// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

import WasmEnableThreads from "consts:wasmEnableThreads";
import BuildConfiguration from "consts:configuration";

import { DotnetModuleInternal, CharPtrNull, MainToWorkerMessageType } from "./types/internal";
import { exportedRuntimeAPI, INTERNAL, loaderHelpers, Module, runtimeHelpers, createPromiseController, mono_assert } from "./globals";
import cwraps, { init_c_exports, threads_c_functions as tcwraps } from "./cwraps";
import { mono_wasm_raise_debug_event, mono_wasm_runtime_ready } from "./debug";
import { toBase64StringImpl } from "./base64";
import { mono_wasm_init_aot_profiler, mono_wasm_init_devtools_profiler, mono_wasm_init_log_profiler } from "./profiler";
import { initialize_marshalers_to_cs } from "./marshal-to-cs";
import { initialize_marshalers_to_js } from "./marshal-to-js";
import { init_polyfills_async } from "./polyfills";
import { strings_init, stringToUTF8Ptr, utf8ToString } from "./strings";
import { init_managed_exports } from "./managed-exports";
import { cwraps_internal } from "./exports-internal";
import { CharPtr, CharPtrPtr, EmscriptenModule, InstantiateWasmCallBack, InstantiateWasmSuccessCallback, VoidPtr } from "./types/emscripten";
import { wait_for_all_assets } from "./assets";
import { replace_linker_placeholders } from "./exports-binding";
import { endMeasure, MeasuredBlock, startMeasure } from "./profiler";
import { interp_pgo_load_data, interp_pgo_save_data } from "./interp-pgo";
import { mono_log_debug, mono_log_error, mono_log_info, mono_log_warn } from "./logging";

// threads
import { populateEmscriptenPool, mono_wasm_init_threads } from "./pthreads";
import { currentWorkerThreadEvents, dotnetPthreadCreated, initWorkerThreadEvents, monoThreadInfo } from "./pthreads";
import { mono_wasm_pthread_ptr, update_thread_info } from "./pthreads";
import { jiterpreter_allocate_tables } from "./jiterpreter-support";
import { localHeapViewU8, malloc, setU32 } from "./memory";
import { assertNoProxies } from "./gc-handles";
import { runtimeList } from "./exports";
import { nativeAbort, nativeExit } from "./run";
import { replaceEmscriptenPThreadInit } from "./pthreads/worker-thread";

// make pid positive 31bit integer based on startup time
const pid = ((globalThis.performance?.timeOrigin ?? Date.now()) | 0) & 0x7FFFFFFF;

export function mono_wasm_process_current_pid ():number {
    return pid;
}

export async function configureRuntimeStartup (module: DotnetModuleInternal): Promise<void> {
    if (!module.out) {
        // eslint-disable-next-line no-console
        module.out = console.log.bind(console);
    }
    if (!module.err) {
        // eslint-disable-next-line no-console
        module.err = console.error.bind(console);
    }
    if (!module.print) {
        module.print = module.out;
    }
    if (!module.printErr) {
        module.printErr = module.err;
    }
    loaderHelpers.out = module.print;
    loaderHelpers.err = module.printErr;
    await init_polyfills_async();
}

// we are making emscripten startup async friendly
// emscripten is executing the events without awaiting it and so we need to block progress via PromiseControllers above
export function configureEmscriptenStartup (module: DotnetModuleInternal): void {
    const mark = startMeasure();

    if (!module.locateFile) {
        // this is dummy plug so that wasmBinaryFile doesn't try to use URL class
        module.locateFile = module.__locateFile = (path) => loaderHelpers.scriptDirectory + path;
    }

    module.mainScriptUrlOrBlob = loaderHelpers.scriptUrl;// this is needed by worker threads

    // these all could be overridden on DotnetModuleConfig, we are chaing them to async below, as opposed to emscripten
    // when user set configSrc or config, we are running our default startup sequence.
    const userInstantiateWasm: undefined | ((imports: WebAssembly.Imports, successCallback: InstantiateWasmSuccessCallback) => any) = module.instantiateWasm;
    const userPreInit: ((module:EmscriptenModule) => void)[] = !module.preInit ? [] : typeof module.preInit === "function" ? [module.preInit] : module.preInit;
    const userPreRun: ((module:EmscriptenModule) => void)[] = !module.preRun ? [] : typeof module.preRun === "function" ? [module.preRun] : module.preRun as any;
    const userpostRun: ((module:EmscriptenModule) => void)[] = !module.postRun ? [] : typeof module.postRun === "function" ? [module.postRun] : module.postRun as any;
    // eslint-disable-next-line @typescript-eslint/no-empty-function
    const userOnRuntimeInitialized: (module:EmscriptenModule) => void = module.onRuntimeInitialized ? module.onRuntimeInitialized : () => { };

    // execution order == [0] ==
    // - default or user Module.instantiateWasm (will start downloading dotnet.native.wasm)
    module.instantiateWasm = (imports, callback) => instantiateWasm(imports, callback, userInstantiateWasm);
    // execution order == [1] ==
    module.preInit = [() => preInit(userPreInit)];
    // execution order == [2] ==
    module.preRun = [() => preRunAsync(userPreRun)];
    // execution order == [4] ==
    module.onRuntimeInitialized = () => onRuntimeInitializedAsync(userOnRuntimeInitialized);
    // execution order == [5] ==
    module.postRun = [() => postRunAsync(userpostRun)];
    // execution order == [6] ==

    module.ready.then(async () => {
        // wait for previous stage
        await runtimeHelpers.afterPostRun.promise;
        // startup end
        endMeasure(mark, MeasuredBlock.emscriptenStartup);
        // - here we resolve the promise returned by createDotnetRuntime export
        // - any code after createDotnetRuntime is executed now
        runtimeHelpers.dotnetReady.promise_control.resolve(exportedRuntimeAPI);
    }).catch(err => {
        runtimeHelpers.dotnetReady.promise_control.reject(err);
    });
    module.ready = runtimeHelpers.dotnetReady.promise;
}

function instantiateWasm (
    imports: WebAssembly.Imports,
    successCallback: InstantiateWasmSuccessCallback,
    userInstantiateWasm?: InstantiateWasmCallBack): any[] {
    // this is called so early that even Module exports like addRunDependency don't exist yet

    const mark = startMeasure();
    if (userInstantiateWasm) {
        const exports = userInstantiateWasm(imports, (instance: WebAssembly.Instance, module: WebAssembly.Module | undefined) => {
            endMeasure(mark, MeasuredBlock.instantiateWasm);
            runtimeHelpers.afterInstantiateWasm.promise_control.resolve();
            successCallback(instance, module);
        });
        return exports;
    }

    instantiate_wasm_module(imports, successCallback);
    return []; // No exports
}

async function instantiateWasmWorker (
    imports: WebAssembly.Imports,
    successCallback: InstantiateWasmSuccessCallback
): Promise<void> {
    if (!WasmEnableThreads) return;

    await ensureUsedWasmFeatures();

    // wait for the config to arrive by message from the main thread
    await loaderHelpers.afterConfigLoaded.promise;

    replace_linker_placeholders(imports);
    replaceEmscriptenPThreadInit();

    // Instantiate from the module posted from the main thread.
    // We can just use sync instantiation in the worker.
    const instance = new WebAssembly.Instance(Module.wasmModule!, imports);
    successCallback(instance, undefined);
    Module.wasmModule = null;
}

function preInit (userPreInit: ((module:EmscriptenModule) => void)[]) {
    Module.addRunDependency("mono_pre_init");
    const mark = startMeasure();
    try {
        mono_wasm_pre_init_essential(false);
        mono_log_debug("preInit");
        runtimeHelpers.beforePreInit.promise_control.resolve();
        // all user Module.preInit callbacks
        userPreInit.forEach(fn => fn(Module));
    } catch (err) {
        mono_log_error("user preInint() failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
    // this will start immediately but return on first await.
    // It will block our `preRun` by afterPreInit promise
    // It will block emscripten `userOnRuntimeInitialized` by pending addRunDependency("mono_pre_init")
    (async () => {
        try {
            // - init the rest of the polyfills
            await mono_wasm_pre_init_essential_async();

            endMeasure(mark, MeasuredBlock.preInit);
        } catch (err) {
            loaderHelpers.mono_exit(1, err);
            throw err;
        }
        // signal next stage
        runtimeHelpers.afterPreInit.promise_control.resolve();
        Module.removeRunDependency("mono_pre_init");
    })();
}

async function preInitWorkerAsync () {
    if (!WasmEnableThreads) return;
    const mark = startMeasure();
    try {
        mono_log_debug("preInitWorker");
        runtimeHelpers.beforePreInit.promise_control.resolve();
        mono_wasm_pre_init_essential(true);
        await ensureUsedWasmFeatures();
        await init_polyfills_async();
        if (loaderHelpers.config.exitOnUnhandledError) {
            loaderHelpers.installUnhandledErrorHandler();
        }
        runtimeHelpers.afterPreInit.promise_control.resolve();
        exportedRuntimeAPI.runtimeId = loaderHelpers.config.runtimeId!;
        runtimeList.registerRuntime(exportedRuntimeAPI);
        endMeasure(mark, MeasuredBlock.preInitWorker);
    } catch (err) {
        mono_log_error("preInitWorker() failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
}

// runs for each re-attached worker
export function preRunWorker () {
    if (!WasmEnableThreads) return;
    const mark = startMeasure();
    try {
        jiterpreter_allocate_tables(); // this will return quickly if already allocated
        runtimeHelpers.nativeExit = nativeExit;
        runtimeHelpers.nativeAbort = nativeAbort;
        runtimeHelpers.runtimeReady = true;
        // signal next stage
        runtimeHelpers.afterPreRun.promise_control.resolve();
        endMeasure(mark, MeasuredBlock.preRunWorker);
    } catch (err) {
        mono_log_error("preRunWorker() failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
}

async function preRunAsync (userPreRun: ((module:EmscriptenModule) => void)[]) {
    Module.addRunDependency("mono_pre_run_async");
    // wait for previous stages
    try {
        await runtimeHelpers.afterInstantiateWasm.promise;
        await runtimeHelpers.afterPreInit.promise;
        mono_log_debug("preRunAsync");
        const mark = startMeasure();
        // all user Module.preRun callbacks
        userPreRun.map(fn => fn(Module));
        endMeasure(mark, MeasuredBlock.preRun);
    } catch (err) {
        mono_log_error("preRunAsync() failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
    // signal next stage
    runtimeHelpers.afterPreRun.promise_control.resolve();
    Module.removeRunDependency("mono_pre_run_async");
}

async function onRuntimeInitializedAsync (userOnRuntimeInitialized: (module:EmscriptenModule) => void) {
    try {
        // wait for previous stage
        await runtimeHelpers.afterPreRun.promise;
        mono_log_debug("onRuntimeInitialized");

        runtimeHelpers.nativeExit = nativeExit;
        runtimeHelpers.nativeAbort = nativeAbort;

        const mark = startMeasure();
        // signal this stage, this will allow pending assets to allocate memory
        runtimeHelpers.beforeOnRuntimeInitialized.promise_control.resolve();

        let threadsReady: Promise<void> | undefined;
        if (WasmEnableThreads) {
            threadsReady = mono_wasm_init_threads();
        }

        await runtimeHelpers.coreAssetsInMemory.promise;

        if (runtimeHelpers.config.virtualWorkingDirectory) {
            const FS = Module.FS;
            const cwd = runtimeHelpers.config.virtualWorkingDirectory;
            try {
                const wds = FS.stat(cwd);
                if (!wds) {
                    Module.FS_createPath("/", cwd, true, true);
                } else {
                    mono_assert(wds && FS.isDir(wds.mode), () => `FS.chdir: ${cwd} is not a directory`);
                }
            } catch (e) {
                Module.FS_createPath("/", cwd, true, true);
            }
            FS.chdir(cwd);
        }

        if (runtimeHelpers.config.interpreterPgo)
            setTimeout(maybeSaveInterpPgoTable, (runtimeHelpers.config.interpreterPgoSaveDelay || 15) * 1000);


        Module.runtimeKeepalivePush();
        if (WasmEnableThreads && BuildConfiguration === "Debug" && globalThis.setInterval) globalThis.setInterval(() => {
            mono_log_info("UI thread is alive!");
        }, 3000);

        if (WasmEnableThreads) {
            await threadsReady;

            // this will create thread and call start_runtime() on it
            runtimeHelpers.monoThreadInfo = monoThreadInfo;
            runtimeHelpers.isManagedRunningOnCurrentThread = false;
            update_thread_info();
            runtimeHelpers.managedThreadTID = tcwraps.mono_wasm_create_deputy_thread();

            // await mono started on deputy thread
            await runtimeHelpers.afterMonoStarted.promise;
            runtimeHelpers.ioThreadTID = tcwraps.mono_wasm_create_io_thread();

            // TODO make UI thread not managed/attached https://github.com/dotnet/runtime/issues/100411
            tcwraps.mono_wasm_register_ui_thread();
            monoThreadInfo.isAttached = true;
            monoThreadInfo.isRegistered = true;

            runtimeHelpers.runtimeReady = true;
            update_thread_info();
            bindings_init();

            tcwraps.mono_wasm_init_finalizer_thread();

            runtimeHelpers.disableManagedTransition = true;
        } else {
            // load mono runtime and apply environment settings (if necessary)
            await start_runtime();
        }

        if (WasmEnableThreads) {
            await runtimeHelpers.afterIOStarted.promise;
        }

        await wait_for_all_assets();

        if (WasmEnableThreads) {
            runtimeHelpers.deputyWorker.thread!.postMessageToWorker({
                type:"deputyThread",
                cmd: MainToWorkerMessageType.allAssetsLoaded,
            });
            runtimeHelpers.proxyGCHandle = await runtimeHelpers.afterDeputyReady.promise;
        }

        runtimeList.registerRuntime(exportedRuntimeAPI);

        if (!runtimeHelpers.mono_wasm_runtime_is_ready) mono_wasm_runtime_ready();

        // call user code
        try {
            userOnRuntimeInitialized(Module);
        } catch (err: any) {
            mono_log_error("user callback onRuntimeInitialized() failed", err);
            throw err;
        }
        // finish
        await mono_wasm_after_user_runtime_initialized();
        endMeasure(mark, MeasuredBlock.onRuntimeInitialized);
    } catch (err) {
        Module.runtimeKeepalivePop();
        mono_log_error("onRuntimeInitializedAsync() failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
    // signal next stage
    runtimeHelpers.afterOnRuntimeInitialized.promise_control.resolve();
}

async function postRunAsync (userpostRun: ((module:EmscriptenModule) => void)[]) {
    // wait for previous stage
    try {
        await runtimeHelpers.afterOnRuntimeInitialized.promise;
        mono_log_debug("postRunAsync");
        const mark = startMeasure();

        // create /usr/share folder which is SpecialFolder.CommonApplicationData
        Module["FS_createPath"]("/", "usr", true, true);
        Module["FS_createPath"]("/", "usr/share", true, true);

        // all user Module.postRun callbacks
        userpostRun.map(fn => fn(Module));
        endMeasure(mark, MeasuredBlock.postRun);
    } catch (err) {
        mono_log_error("postRunAsync() failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
    // signal next stage
    runtimeHelpers.afterPostRun.promise_control.resolve();
}

// runs for each re-detached worker
export function postRunWorker () {
    if (!WasmEnableThreads) return;
    const mark = startMeasure();
    try {
        if (runtimeHelpers.proxyGCHandle) {
            mono_log_warn("JSSynchronizationContext is still installed on worker.");
        } else {
            assertNoProxies();
        }

        // signal next stage
        runtimeHelpers.runtimeReady = false;
        runtimeHelpers.afterPreRun = createPromiseController<void>();
        endMeasure(mark, MeasuredBlock.postRunWorker);
    } catch (err) {
        mono_log_error("postRunWorker() failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
}

function mono_wasm_pre_init_essential (isWorker: boolean): void {
    if (!isWorker)
        Module.addRunDependency("mono_wasm_pre_init_essential");

    mono_log_debug("mono_wasm_pre_init_essential");

    if (loaderHelpers.gitHash !== runtimeHelpers.gitHash) {
        mono_log_warn(`The version of dotnet.runtime.js ${runtimeHelpers.gitHash} is different from the version of dotnet.js ${loaderHelpers.gitHash}!`);
    }
    if (loaderHelpers.gitHash !== runtimeHelpers.emscriptenBuildOptions.gitHash) {
        mono_log_warn(`The version of dotnet.native.js ${runtimeHelpers.emscriptenBuildOptions.gitHash}  is different from the version of dotnet.js ${loaderHelpers.gitHash}!`);
    }
    if (WasmEnableThreads !== runtimeHelpers.emscriptenBuildOptions.wasmEnableThreads) {
        mono_log_warn(`The threads of dotnet.native.js ${runtimeHelpers.emscriptenBuildOptions.wasmEnableThreads} is different from the version of dotnet.runtime.js ${WasmEnableThreads}!`);
    }

    init_c_exports();
    cwraps_internal(INTERNAL);
    // removeRunDependency triggers the dependenciesFulfilled callback (runCaller) in
    // emscripten - on a worker since we don't have any other dependencies that causes run() to get
    // called too soon; and then it will get called a second time when dotnet.native.js calls it directly.
    // on a worker run() short-cirtcuits and just calls   readyPromiseResolve, initRuntime and postMessage.
    // sending postMessage twice will break instantiateWasmPThreadWorkerPool on the main thread.
    if (!isWorker)
        Module.removeRunDependency("mono_wasm_pre_init_essential");
}

async function mono_wasm_pre_init_essential_async (): Promise<void> {
    mono_log_debug("mono_wasm_pre_init_essential_async");
    Module.addRunDependency("mono_wasm_pre_init_essential_async");

    if (WasmEnableThreads) {
        await populateEmscriptenPool();
    }

    Module.removeRunDependency("mono_wasm_pre_init_essential_async");
}

async function mono_wasm_after_user_runtime_initialized (): Promise<void> {
    mono_log_debug("mono_wasm_after_user_runtime_initialized");
    try {
        if (Module.onDotnetReady) {
            try {
                await Module.onDotnetReady();
            } catch (err: any) {
                mono_log_error("onDotnetReady () failed", err);
                throw err;
            }
        }
    } catch (err: any) {
        mono_log_error("mono_wasm_after_user_runtime_initialized () failed", err);
        throw err;
    }
}

// Set environment variable NAME to VALUE
// Should be called before mono_load_runtime_and_bcl () in most cases
export function mono_wasm_setenv (name: string, value: string): void {
    cwraps.mono_wasm_setenv(name, value);
}

export function mono_wasm_set_runtime_options (options: string[]): void {
    if (!Array.isArray(options))
        throw new Error("Expected runtimeOptions to be an array of strings");

    const argv = malloc(options.length * 4);
    let aindex = 0;
    for (let i = 0; i < options.length; ++i) {
        const option = options[i];
        if (typeof (option) !== "string")
            throw new Error("Expected runtimeOptions to be an array of strings");
        Module.setValue(<any>argv + (aindex * 4), cwraps.mono_wasm_strdup(option), "i32");
        aindex += 1;
    }
    cwraps.mono_wasm_parse_runtime_options(options.length, argv);
}

async function instantiate_wasm_module (
    imports: WebAssembly.Imports,
    successCallback: InstantiateWasmSuccessCallback,
): Promise<void> {
    // this is called so early that even Module exports like addRunDependency don't exist yet
    try {
        await loaderHelpers.afterConfigLoaded;
        mono_log_debug("instantiate_wasm_module");

        await runtimeHelpers.beforePreInit.promise;
        Module.addRunDependency("instantiate_wasm_module");

        await ensureUsedWasmFeatures();

        replace_linker_placeholders(imports);
        const compiledModule = await loaderHelpers.wasmCompilePromise.promise;
        const compiledInstance = await WebAssembly.instantiate(compiledModule, imports);
        successCallback(compiledInstance, compiledModule);

        mono_log_debug("instantiate_wasm_module done");

        runtimeHelpers.afterInstantiateWasm.promise_control.resolve();
    } catch (err) {
        mono_log_error("instantiate_wasm_module() failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
    Module.removeRunDependency("instantiate_wasm_module");
}

async function ensureUsedWasmFeatures () {
    const simd = loaderHelpers.simd();
    const relaxedSimd = loaderHelpers.relaxedSimd();
    const exceptions = loaderHelpers.exceptions();
    runtimeHelpers.featureWasmSimd = await simd;
    runtimeHelpers.featureWasmRelaxedSimd = await relaxedSimd;
    runtimeHelpers.featureWasmEh = await exceptions;
    if (runtimeHelpers.emscriptenBuildOptions.wasmEnableSIMD) {
        mono_assert(runtimeHelpers.featureWasmSimd, "This browser/engine doesn't support WASM SIMD. Please use a modern version. See also https://aka.ms/dotnet-wasm-features");
    }
    if (runtimeHelpers.emscriptenBuildOptions.wasmEnableEH) {
        mono_assert(runtimeHelpers.featureWasmEh, "This browser/engine doesn't support WASM exception handling. Please use a modern version. See also https://aka.ms/dotnet-wasm-features");
    }
}

export async function start_runtime () {
    try {
        const mark = startMeasure();
        const environmentVariables = runtimeHelpers.config.environmentVariables || {};
        mono_log_debug("Initializing mono runtime");
        for (const k in environmentVariables) {
            const v = environmentVariables![k];
            if (typeof (v) === "string")
                mono_wasm_setenv(k, v);
            else
                throw new Error(`Expected environment variable '${k}' to be a string but it was ${typeof v}: '${v}'`);
        }
        if (runtimeHelpers.config.runtimeOptions)
            mono_wasm_set_runtime_options(runtimeHelpers.config.runtimeOptions);

        if (runtimeHelpers.emscriptenBuildOptions.enableEventPipe) {
            const diagnosticPorts = "DOTNET_DiagnosticPorts";
            // connect JS client by default
            const jsReady = "js://ready";
            if (!environmentVariables[diagnosticPorts]) {
                environmentVariables[diagnosticPorts] = jsReady;
                mono_wasm_setenv(diagnosticPorts, jsReady);
            }
        } else if (runtimeHelpers.emscriptenBuildOptions.enableAotProfiler) {
            mono_wasm_init_aot_profiler(runtimeHelpers.config.aotProfilerOptions || {});
        } else if (runtimeHelpers.emscriptenBuildOptions.enableDevToolsProfiler) {
            mono_wasm_init_devtools_profiler();
        } else if (runtimeHelpers.emscriptenBuildOptions.enableLogProfiler) {
            mono_wasm_init_log_profiler(runtimeHelpers.config.logProfilerOptions || {});
        }

        mono_wasm_load_runtime();

        jiterpreter_allocate_tables();

        bindings_init();

        runtimeHelpers.runtimeReady = true;

        if (WasmEnableThreads) {
            monoThreadInfo.isAttached = true;
            monoThreadInfo.isRunning = true;
            monoThreadInfo.isRegistered = true;
            runtimeHelpers.currentThreadTID = monoThreadInfo.pthreadId = runtimeHelpers.managedThreadTID = mono_wasm_pthread_ptr();
            update_thread_info();
            runtimeHelpers.isManagedRunningOnCurrentThread = true;
        }

        // get GCHandle of the ctx
        runtimeHelpers.afterMonoStarted.promise_control.resolve();

        if (runtimeHelpers.config.interpreterPgo) {
            await interp_pgo_load_data();
        }

        endMeasure(mark, MeasuredBlock.startRuntime);
    } catch (err) {
        mono_log_error("start_runtime() failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
}

async function maybeSaveInterpPgoTable () {
    // If the application exited abnormally, don't save the table. It probably doesn't contain useful data,
    //  and saving would overwrite any existing table from a previous successful run.
    // We treat exiting with a code of 0 as equivalent to if the app is still running - it's perfectly fine
    //  to save the table once main has returned, since the table can still make future runs faster.
    if ((loaderHelpers.exitCode !== undefined) && (loaderHelpers.exitCode !== 0))
        return;

    await interp_pgo_save_data();
}

export function mono_wasm_load_runtime (): void {
    mono_log_debug("mono_wasm_load_runtime");
    try {
        const mark = startMeasure();
        let debugLevel = runtimeHelpers.config.debugLevel;
        if (debugLevel == undefined) {
            debugLevel = 0;
            if (runtimeHelpers.config.debugLevel) {
                debugLevel = 0 + debugLevel;
            }
        }
        if (!loaderHelpers.isDebuggingSupported() || !(runtimeHelpers.config.resources!.corePdb || runtimeHelpers.config.resources!.pdb)) {
            debugLevel = 0;
        }

        const runtimeConfigProperties = new Map<string, string>();
        if (runtimeHelpers.config.runtimeConfig?.runtimeOptions?.configProperties) {
            for (const [key, value] of Object.entries(runtimeHelpers.config.runtimeConfig?.runtimeOptions?.configProperties)) {
                runtimeConfigProperties.set(key, "" + value);
            }
        }
        runtimeConfigProperties.set("APP_CONTEXT_BASE_DIRECTORY", "/");
        runtimeConfigProperties.set("RUNTIME_IDENTIFIER", "browser-wasm");
        const propertyCount = runtimeConfigProperties.size;

        const buffers:VoidPtr[] = [];
        const appctx_keys = malloc(4 * runtimeConfigProperties.size) as any as CharPtrPtr;
        const appctx_values = malloc(4 * runtimeConfigProperties.size) as any as CharPtrPtr;
        buffers.push(appctx_keys as any);
        buffers.push(appctx_values as any);

        let position = 0;
        for (const [key, value] of runtimeConfigProperties.entries()) {
            const keyPtr = stringToUTF8Ptr(key);
            const valuePtr = stringToUTF8Ptr(value);
            setU32((appctx_keys as any) + (position * 4), keyPtr);
            setU32((appctx_values as any) + (position * 4), valuePtr);
            position++;
            buffers.push(keyPtr as any);
            buffers.push(valuePtr as any);
        }

        cwraps.mono_wasm_load_runtime(debugLevel, propertyCount, appctx_keys, appctx_values);

        // free the buffers
        for (const buffer of buffers) {
            Module._free(buffer);
        }

        endMeasure(mark, MeasuredBlock.loadRuntime);

    } catch (err: any) {
        mono_log_error("mono_wasm_load_runtime () failed", err);
        loaderHelpers.mono_exit(1, err);
        throw err;
    }
}

export function bindings_init (): void {
    if (runtimeHelpers.mono_wasm_bindings_is_ready) {
        return;
    }
    mono_log_debug("bindings_init");
    runtimeHelpers.mono_wasm_bindings_is_ready = true;
    try {
        const mark = startMeasure();
        strings_init();
        init_managed_exports();
        initialize_marshalers_to_js();
        initialize_marshalers_to_cs();
        runtimeHelpers._i52_error_scratch_buffer = <any>malloc(4);
        endMeasure(mark, MeasuredBlock.bindingsInit);
    } catch (err) {
        mono_log_error("Error in bindings_init", err);
        throw err;
    }
}


export function mono_wasm_asm_loaded (assembly_name: CharPtr, assembly_ptr: number, assembly_len: number, pdb_ptr: number, pdb_len: number): void {
    // Only trigger this codepath for assemblies loaded after app is ready
    if (runtimeHelpers.mono_wasm_runtime_is_ready !== true)
        return;
    const heapU8 = localHeapViewU8();
    const assembly_name_str = assembly_name !== CharPtrNull ? utf8ToString(assembly_name).concat(".dll") : "";
    const assembly_data = new Uint8Array(heapU8.buffer, assembly_ptr, assembly_len);
    const assembly_b64 = toBase64StringImpl(assembly_data);

    let pdb_b64;
    if (pdb_ptr) {
        const pdb_data = new Uint8Array(heapU8.buffer, pdb_ptr, pdb_len);
        pdb_b64 = toBase64StringImpl(pdb_data);
    }

    mono_wasm_raise_debug_event({
        eventName: "AssemblyLoaded",
        assembly_name: assembly_name_str,
        assembly_b64,
        pdb_b64
    });
}

export function mono_wasm_set_main_args (name: string, allRuntimeArguments: string[]): void {
    const main_argc = allRuntimeArguments.length + 1;
    const main_argv = <any>malloc(main_argc * 4);
    let aindex = 0;
    Module.setValue(main_argv + (aindex * 4), cwraps.mono_wasm_strdup(name), "i32");
    aindex += 1;
    for (let i = 0; i < allRuntimeArguments.length; ++i) {
        Module.setValue(main_argv + (aindex * 4), cwraps.mono_wasm_strdup(allRuntimeArguments[i]), "i32");
        aindex += 1;
    }
    cwraps.mono_wasm_set_main_args(main_argc, main_argv);
}

/// Called when dotnet.worker.js receives an emscripten "load" event from the main thread.
/// This method is comparable to configure_emscripten_startup function
///
/// Notes:
/// 1. Emscripten skips a lot of initialization on the pthread workers, Module may not have everything you expect.
/// 2. Emscripten does not run any event but preInit in the workers.
/// 3. At the point when this executes there is no pthread assigned to the worker yet.
export async function configureWorkerStartup (module: DotnetModuleInternal): Promise<void> {
    if (!WasmEnableThreads) return;

    initWorkerThreadEvents();
    currentWorkerThreadEvents.addEventListener(dotnetPthreadCreated, () => {
        // mono_log_debug("pthread created 0x" + ev.pthread_self.pthreadId.toString(16));
    });

    // these are the only events which are called on worker
    module.preInit = [() => preInitWorkerAsync()];
    module.instantiateWasm = instantiateWasmWorker;
    await runtimeHelpers.afterPreInit.promise;
}
