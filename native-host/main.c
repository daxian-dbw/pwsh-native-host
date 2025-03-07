
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#endif

#include <nethost.h>

#include <coreclrhost.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>

#define HOSTFXR_MAX_PATH    1024

static void* load_library(const char* path)
{
#ifdef _WIN32
    HMODULE hModule = LoadLibraryA(path);
    return (void*) hModule;
#else
    void* handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    return handle;
#endif
}

static void* get_proc_address(void* handle, const char* name)
{
#ifdef _WIN32
    HMODULE hModule = (HMODULE) handle;
    void* symbol = GetProcAddress(hModule, name);
    return symbol;
#else
    void* symbol = dlsym(handle, name);
    return symbol;
#endif
}

static uint8_t* load_file(const char* filename, size_t* size)
{
	FILE* fp = NULL;
	uint8_t* data = NULL;

	if (!filename || !size)
		return NULL;

	*size = 0;

	fp = fopen(filename, "rb");

	if (!fp)
		return NULL;

	fseek(fp, 0, SEEK_END);
	*size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	data = malloc(*size);

	if (!data)
		goto exit;

	if (fread(data, 1, *size, fp) != *size)
	{
		free(data);
		data = NULL;
		*size = 0;
	}

exit:
	fclose(fp);
	return data;
}

#ifndef _WIN32
extern void pthread_create();

void linker_dummy()
{
    // force linking pthread library
    pthread_create();
}
#endif

struct coreclr_context
{
    coreclr_initialize_fn initialize;
    coreclr_shutdown_fn shutdown;
    coreclr_shutdown_2_fn shutdown_2;
    coreclr_create_delegate_fn create_delegate;
    coreclr_execute_assembly_fn execute_assembly;
};
typedef struct coreclr_context CORECLR_CONTEXT;

bool load_coreclr(CORECLR_CONTEXT* coreclr, const char* coreclr_path)
{
    void* lib_handle = load_library(coreclr_path);

    memset(coreclr, 0, sizeof(CORECLR_CONTEXT));

    if (!lib_handle) {
        printf("could not load %s\n", coreclr_path);
    }

    coreclr->initialize = (coreclr_initialize_fn) get_proc_address(lib_handle, "coreclr_initialize");
    coreclr->shutdown = (coreclr_shutdown_fn) get_proc_address(lib_handle, "coreclr_shutdown");
    coreclr->shutdown_2 = (coreclr_shutdown_2_fn) get_proc_address(lib_handle, "coreclr_shutdown_2");
    coreclr->create_delegate = (coreclr_create_delegate_fn) get_proc_address(lib_handle, "coreclr_create_delegate");
    coreclr->execute_assembly = (coreclr_execute_assembly_fn) get_proc_address(lib_handle, "coreclr_execute_assembly");

    if (!coreclr->initialize || !coreclr->shutdown || !coreclr->shutdown_2 ||
        !coreclr->create_delegate || !coreclr->execute_assembly)
    {
        printf("could not load CoreCLR functions\n");
        return false;
    }

    return true;
}

struct hostfxr_context
{
    hostfxr_initialize_for_dotnet_command_line_fn initialize_for_dotnet_command_line;
    hostfxr_initialize_for_runtime_config_fn initialize_for_runtime_config;
    hostfxr_get_runtime_property_value_fn get_runtime_property_value;
    hostfxr_set_runtime_property_value_fn set_runtime_property_value;
    hostfxr_get_runtime_properties_fn get_runtime_properties;
    hostfxr_run_app_fn run_app;
    hostfxr_get_runtime_delegate_fn get_runtime_delegate;
    hostfxr_close_fn close;

    load_assembly_and_get_function_pointer_fn load_assembly_and_get_function_pointer;
    get_function_pointer_fn get_function_pointer;
    hostfxr_handle context_handle;
};
typedef struct hostfxr_context HOSTFXR_CONTEXT;

bool load_hostfxr(HOSTFXR_CONTEXT* hostfxr, const char* hostfxr_path)
{
    void* lib_handle = load_library(hostfxr_path);

    memset(hostfxr, 0, sizeof(HOSTFXR_CONTEXT));

    if (!lib_handle) {
        printf("could not load %s\n", hostfxr_path);
    }

    hostfxr->initialize_for_dotnet_command_line = (hostfxr_initialize_for_dotnet_command_line_fn)
        get_proc_address(lib_handle, "hostfxr_initialize_for_dotnet_command_line");
    hostfxr->initialize_for_runtime_config = (hostfxr_initialize_for_runtime_config_fn)
        get_proc_address(lib_handle, "hostfxr_initialize_for_runtime_config");
    hostfxr->get_runtime_property_value = (hostfxr_get_runtime_property_value_fn)
        get_proc_address(lib_handle, "hostfxr_get_runtime_property_value");
    hostfxr->set_runtime_property_value = (hostfxr_set_runtime_property_value_fn)
        get_proc_address(lib_handle, "hostfxr_set_runtime_property_value");
    hostfxr->get_runtime_properties = (hostfxr_get_runtime_properties_fn)
        get_proc_address(lib_handle, "hostfxr_get_runtime_properties");
    hostfxr->run_app = (hostfxr_run_app_fn)
        get_proc_address(lib_handle, "hostfxr_run_app");
    hostfxr->get_runtime_delegate = (hostfxr_get_runtime_delegate_fn)
        get_proc_address(lib_handle, "hostfxr_get_runtime_delegate");
    hostfxr->close = (hostfxr_close_fn)
        get_proc_address(lib_handle, "hostfxr_close");

    return true;
}

bool load_runtime(HOSTFXR_CONTEXT* hostfxr, const char* config_path)
{
    hostfxr_handle ctx = NULL;
    load_assembly_and_get_function_pointer_fn load_assembly_and_get_function_pointer = NULL;

    int rc = hostfxr->initialize_for_runtime_config(config_path, NULL, &ctx);

    if ((rc != 0) || (ctx == NULL)) {
        printf("initialize_for_runtime_config(%s) failure\n", config_path);
        return false;
    }

    rc = hostfxr->get_runtime_delegate(ctx,
        hdt_load_assembly_and_get_function_pointer,
        (void**) &load_assembly_and_get_function_pointer);

    if ((rc != 0) || (NULL == load_assembly_and_get_function_pointer)) {
        printf("get_runtime_delegate failure\n");
        return false;
    }

    hostfxr->close(ctx);

    hostfxr->load_assembly_and_get_function_pointer = load_assembly_and_get_function_pointer;

    return true;
}

typedef void (CORECLR_DELEGATE_CALLTYPE * fnLoadAssemblyData)(uint8_t* bytes, int32_t size);
typedef void (CORECLR_DELEGATE_CALLTYPE * fnLoadAssemblyFile)(const char* filename);

static fnLoadAssemblyData g_LoadAssemblyData = NULL;
static fnLoadAssemblyFile g_LoadAssemblyFile = NULL;

bool load_assembly_helper(HOSTFXR_CONTEXT* hostfxr, const char* helper_path, const char* type_name)
{
    int rc;

    rc =  hostfxr->load_assembly_and_get_function_pointer(helper_path,
        type_name, "LoadAssemblyFromMemory",
        UNMANAGEDCALLERSONLY_METHOD, NULL, (void**) &g_LoadAssemblyData);

    if (rc != 0) {
        printf("load_assembly_and_get_function_pointer(LoadAssemblyFromMemory): 0x%08X\n", rc);
        return false;
    }

#if 0
    rc = hostfxr->load_assembly_and_get_function_pointer(helper_path,
        type_name, "LoadAssemblyFile",
        UNMANAGEDCALLERSONLY_METHOD, NULL, (void**) &g_LoadAssemblyFile);

    if (rc != 0) {
        printf("load_assembly_and_get_function_pointer(LoadAssemblyFile): 0x%08X\n", rc);
        return false;
    }
#endif

    return true;
}

typedef void* hPowerShell;
typedef hPowerShell (CORECLR_DELEGATE_CALLTYPE * fnPowerShell_Create)(void);
typedef void (CORECLR_DELEGATE_CALLTYPE * fnPowerShell_AddScript)(hPowerShell handle, const char* script);
typedef void (CORECLR_DELEGATE_CALLTYPE * fnPowerShell_Invoke)(hPowerShell handle);

typedef struct
{
    fnPowerShell_Create Create;
    fnPowerShell_AddScript AddScript;
    fnPowerShell_Invoke Invoke;
} iPowerShell;

bool load_pwsh_sdk(HOSTFXR_CONTEXT* hostfxr, const char* assembly_path, iPowerShell* iface)
{
    int rc;
    memset(iface, 0, sizeof(iPowerShell));

    if (0)
    {
         hostfxr->load_assembly_and_get_function_pointer(assembly_path,
            "NativeHost.Bindings, NativeHost", "PowerShell_Create",
            UNMANAGEDCALLERSONLY_METHOD, NULL, (void**) &iface->Create);

        if (rc != 0) {
            printf("load_assembly_and_get_function_pointer: 0x%08X\n", rc);
            return false;
        }

        hostfxr->load_assembly_and_get_function_pointer(assembly_path,
            "NativeHost.Bindings, NativeHost", "PowerShell_AddScript",
            UNMANAGEDCALLERSONLY_METHOD, NULL, (void**) &iface->AddScript);

        hostfxr->load_assembly_and_get_function_pointer(assembly_path,
            "NativeHost.Bindings, NativeHost", "PowerShell_Invoke",
            UNMANAGEDCALLERSONLY_METHOD, NULL, (void**) &iface->Invoke);   
    }
    else
    {
        uint8_t* assembly_data = NULL;
        size_t assembly_size = 0;

        assembly_data = load_file(assembly_path, &assembly_size);
        
        if (!assembly_data) {
            printf("couldn't load %s\n", assembly_path);
            return false;
        }

        printf("loaded %s (%d bytes)\n", assembly_path, (int) assembly_size);

        g_LoadAssemblyData(assembly_data, (int32_t) assembly_size);
        free(assembly_data);

        rc = hostfxr->get_function_pointer(
            "NativeHost.Bindings, NativeHost", "PowerShell_Create",
            UNMANAGEDCALLERSONLY_METHOD, NULL, NULL, (void**) &iface->Create);

        if (rc != 0) {
            printf("get_function_pointer failure: 0x%08X\n", rc);
            return false;
        }

        rc = hostfxr->get_function_pointer(
            "NativeHost.Bindings, NativeHost", "PowerShell_AddScript",
            UNMANAGEDCALLERSONLY_METHOD, NULL, NULL, (void**) &iface->AddScript);

        rc = hostfxr->get_function_pointer(
            "NativeHost.Bindings, NativeHost", "PowerShell_Invoke",
            UNMANAGEDCALLERSONLY_METHOD, NULL, NULL, (void**) &iface->Invoke);
    }

    return true;
}

bool call_pwsh_sdk(HOSTFXR_CONTEXT* hostfxr, const char* assembly_path)
{
    iPowerShell iface;

    load_pwsh_sdk(hostfxr, assembly_path, &iface);

    hPowerShell handle = iface.Create();
    iface.AddScript(handle, "Set-Content -Path '/tmp/pwsh-date.txt' -Value \"Microsoft.PowerShell.SDK: $(Get-Date)\"");
    iface.AddScript(handle, "$(lsof -p $PID | grep dll) > /tmp/pwsh-lsof.txt");
    iface.Invoke(handle);

    return true;
}

bool run_sample()
{
    HOSTFXR_CONTEXT hostfxr;
    char base_path[HOSTFXR_MAX_PATH];
    char runtime_config_path[HOSTFXR_MAX_PATH];
    char assembly_path[HOSTFXR_MAX_PATH];

    const char* hostfxr_path = "/usr/share/dotnet/host/fxr/5.0.0/libhostfxr.so";

    if (!load_hostfxr(&hostfxr, hostfxr_path)) {
        printf("failed to load hostfxr!\n");
        return -1;
    }

    strncpy(base_path, "/opt/wayk/dev/pwsh-native-host/NativeHost/bin/Release/net5.0", HOSTFXR_MAX_PATH);
    snprintf(runtime_config_path, HOSTFXR_MAX_PATH, "%s/%s.runtimeconfig.json", base_path, "NativeHost");
    snprintf(assembly_path, HOSTFXR_MAX_PATH, "%s/%s.dll", base_path, "NativeHost");

    if (!load_runtime(&hostfxr, runtime_config_path)) {
        printf("failed to load runtime!\n");
        return -1;
    }

    call_pwsh_sdk(&hostfxr, assembly_path);
}

bool load_command(HOSTFXR_CONTEXT* hostfxr, int argc, const char** argv)
{
    hostfxr_handle ctx = NULL;
    load_assembly_and_get_function_pointer_fn load_assembly_and_get_function_pointer = NULL;
    get_function_pointer_fn get_function_pointer = NULL;

    int rc = hostfxr->initialize_for_dotnet_command_line(argc, argv, NULL, &ctx);

    if ((rc != 0) || (ctx == NULL)) {
        printf("hostfxr->initialize_for_dotnet_command_line() failure: 0x%08X\n", rc);
        return false;
    }

    rc = hostfxr->get_runtime_delegate(ctx,
        hdt_load_assembly_and_get_function_pointer,
        (void**) &load_assembly_and_get_function_pointer);

    if ((rc != 0) || (NULL == load_assembly_and_get_function_pointer)) {
        printf("get_runtime_delegate failure: 0x%08X\n", rc);
        return false;
    }

    rc = hostfxr->get_runtime_delegate(ctx,
        hdt_get_function_pointer,
        (void**) &get_function_pointer);

    if ((rc != 0) || (NULL == get_function_pointer)) {
        printf("get_runtime_delegate failure: 0x%08X\n", rc);
        return false;
    }

    hostfxr->context_handle = ctx;
    //hostfxr->close(ctx);

    hostfxr->load_assembly_and_get_function_pointer = load_assembly_and_get_function_pointer;
    hostfxr->get_function_pointer = get_function_pointer;

    return true;
}

bool run_pwsh_app()
{
    HOSTFXR_CONTEXT hostfxr;
    char base_path[HOSTFXR_MAX_PATH];
    char hostfxr_path[HOSTFXR_MAX_PATH];
    char runtime_config_path[HOSTFXR_MAX_PATH];
    char assembly_path[HOSTFXR_MAX_PATH];

    strncpy(base_path, "/home/wayk/powershell-7.1.0", HOSTFXR_MAX_PATH);
    strncpy(base_path, "/opt/microsoft/powershell/7", HOSTFXR_MAX_PATH);
    snprintf(hostfxr_path, HOSTFXR_MAX_PATH, "%s/libhostfxr.so", base_path);

    if (!load_hostfxr(&hostfxr, hostfxr_path)) {
        printf("failed to load hostfxr!\n");
        return false;
    }

    snprintf(runtime_config_path, HOSTFXR_MAX_PATH, "%s/%s.runtimeconfig.json", base_path, "pwsh");
    snprintf(assembly_path, HOSTFXR_MAX_PATH, "%s/%s.dll", base_path, "pwsh");

    char* command_args[] = {
        assembly_path,
        "-NoLogo",
        "-NoExit",
        "-Command",
        "Write-Host 'Hello PowerShell Host'"
    };
    int command_argc = sizeof(command_args) / sizeof(char*);

    if (!load_command(&hostfxr, command_argc, (const char**) command_args)) {
        printf("failed to load runtime!\n");
        return false;
    }

    hostfxr.run_app(hostfxr.context_handle);

    return true;
}

bool run_pwsh_lib()
{
    HOSTFXR_CONTEXT hostfxr;
    CORECLR_CONTEXT coreclr;
    char base_path[HOSTFXR_MAX_PATH];
    char hostfxr_path[HOSTFXR_MAX_PATH];
    char coreclr_path[HOSTFXR_MAX_PATH];
    char runtime_config_path[HOSTFXR_MAX_PATH];
    char assembly_path[HOSTFXR_MAX_PATH];

    strncpy(base_path, "/home/wayk/powershell-7.1.0", HOSTFXR_MAX_PATH);
    strncpy(base_path, "/opt/microsoft/powershell/7", HOSTFXR_MAX_PATH);
    strncpy(base_path, "/opt/wayk/dev/powershell/src/powershell-unix/bin/Debug/net5.0/linux-x64", HOSTFXR_MAX_PATH);
    snprintf(hostfxr_path, HOSTFXR_MAX_PATH, "%s/libhostfxr.so", base_path);
    snprintf(coreclr_path, HOSTFXR_MAX_PATH, "%s/libcoreclr.so", base_path);

    if (!load_hostfxr(&hostfxr, hostfxr_path)) {
        printf("failed to load hostfxr!\n");
        return false;
    }

    if (!load_coreclr(&coreclr, coreclr_path)) {
        printf("failed to load coreclr!\n");
        return false;
    }

    snprintf(runtime_config_path, HOSTFXR_MAX_PATH, "%s/%s.runtimeconfig.json", base_path, "pwsh");
    snprintf(assembly_path, HOSTFXR_MAX_PATH, "%s/%s.dll", base_path, "pwsh");

    printf("loading %s\n", runtime_config_path);

    char* command_args[] = {
        assembly_path
    };
    int command_argc = sizeof(command_args) / sizeof(char*);

    if (!load_command(&hostfxr, command_argc, (const char**) command_args)) {
        printf("failed to load runtime!\n");
        return false;
    }

    char helper_base_path[HOSTFXR_MAX_PATH];
    char helper_assembly_path[HOSTFXR_MAX_PATH];

    if (1)
    {
        snprintf(helper_assembly_path, HOSTFXR_MAX_PATH, "%s/System.Management.Automation.dll", base_path);
        load_assembly_helper(&hostfxr, helper_assembly_path, "System.Management.Automation.PowerShellUnsafeAssemblyLoad, System.Management.Automation");

        //snprintf(helper_assembly_path, HOSTFXR_MAX_PATH, "%s/Microsoft.PowerShell.ConsoleHost.dll", base_path);
        //load_assembly_helper(&hostfxr, helper_assembly_path, "Microsoft.PowerShell.NativeHost, Microsoft.PowerShell.ConsoleHost");
    }
    else
    {
        strncpy(helper_base_path, "/opt/wayk/dev/pwsh-native-host/NativeHelper/bin/Release/net5.0", HOSTFXR_MAX_PATH);
        snprintf(helper_assembly_path, HOSTFXR_MAX_PATH, "%s/%s.dll", helper_base_path, "NativeHelper");
        load_assembly_helper(&hostfxr, helper_assembly_path, "NativeHelper.Bindings, NativeHelper");
    }

    char host_base_path[HOSTFXR_MAX_PATH];
    char host_assembly_path[HOSTFXR_MAX_PATH];

    strncpy(host_base_path, "/opt/wayk/dev/pwsh-native-host/NativeHost/bin/Release/net5.0", HOSTFXR_MAX_PATH);
    snprintf(host_assembly_path, HOSTFXR_MAX_PATH, "%s/%s.dll", host_base_path, "NativeHost");

    call_pwsh_sdk(&hostfxr, host_assembly_path);

    return true;
}

int main(int argc, char** argv)
{
    //run_sample();
    //run_pwsh_app();
    run_pwsh_lib();

    return 0;
}
