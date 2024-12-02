#include <Windows.h>
#include <winuser.h>
#include <tchar.h>
#include <string>
#include <stdio.h>
#include <vector>
#include <wtsapi32.h>
#include <regex>

#define BUFSIZE 4096

using namespace std;

#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
#define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue(13, FALSE, TRUE, FALSE);
#endif

SERVICE_STATUS g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent INVALID_HANDLE_VALUE;
vector<PROCESS_INFORMATION> g_ProcInfos;
vector<wstring> g_DisplayNames;

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);
VOID WINAPI GetDisplayNames();

#define SERVICE_NAME _T("Sunshine Frostfilms")

struct SunshineInstance{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    WCHAR cmd_args[2048];
};

vector<SunshineInstance> sunshineInstances;

HANDLE OpenLogFileHandle(WCHAR* filename){
    WCHAR log_file_name[MAX_PATH];
    GetTempPathW(_countof(log_file_name), log_file_name);
    wcscat_s(log_file_name, filename);

    SECURITY_ATTRIBUTES security_attributes = { sizeof(security_attributes), NULL, TRUE };
    OutputDebugStringW(log_file_name);
    return CreateFileW(log_file_name,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &security_attributes,
        CREATE_ALWAYS,
        0,
        NULL
    );
}

VOID WINAPI GetDisplayNames(){
    HANDLE dxgi_OUT_RD = NULL;
    HANDLE dxgi_OUT_WR = NULL;
    SECURITY_ATTRIBUTES sec_attr;
    sec_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sec_attr.bInheritHandle = TRUE;
    sec_attr.lpSecurityDescriptor = NULL;

    if(!CreatePipe(&dxgi_OUT_RD, &dxgi_OUT_WR, &sec_attr, 0)){
        OutputDebugStringW(L"Error createing pipe for dxgi.exe stdout"); return;
    }

    if(!SetHandleInformation(dxgi_OUT_RD, HANDLE_FLAG_INHERIT, 0)){
        OutputDebugStringW(L"Error setting pipe attriute for dxgi.exe stdout");
    }

    TCHAR szCmd[] = TEXT("tools\\dxgi-info.exe");
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    BOOL bSuccess = FALSE;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));

    DWORD sessionID = WTSGetActiveConsoleSessionId();
    HANDLE htoken;
    WTSQueryUserToken(sessionID, &htoken);

    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = dxgi_OUT_WR;
    siStartInfo.hStdOutput = dxgi_OUT_WR;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    OutputDebugStringW(L"Creating dxgi process now");
    bSuccess = CreateProcessAsUser(
        htoken,
        szCmd,
        NULL,
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &siStartInfo,
        &piProcInfo
    );

    if(!bSuccess){
        OutputDebugStringW(L"Failed to Create dxgi Process");
    }else{
        WaitForSingleObject(piProcInfo.hProcess, INFINITE);
        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);
        CloseHandle(dxgi_OUT_WR);

        DWORD dwRead, dwWritten;
        CHAR chBuf[BUFSIZE];
        BOOL readSuccess = FALSE;
        for(;;){
            readSuccess = ReadFile(dxgi_OUT_RD, chBuf, BUFSIZE, &dwRead, NULL);
            if(!readSuccess || dwRead == 0){ break; }
        }

        regex matchDisplays("(DISPLAY\\d+)");
        string strInfo(chBuf);

        OutputDebugStringW(L"regex matches:");
        for(
            sregex_iterator it = sregex_iterator(strInfo.begin(), strInfo.end(), matchDisplays);
            it != sregex_iterator();
            it++
        ){
            smatch match;
            match = *it;
            string s = match.str(0);
            wstring ws = wstring(s.begin(), s.end());

            OutputDebugStringW( ws.c_str());
            g_DisplayNames.push_back(ws);
        }
    }
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode){
    OutputDebugString(_T("ServiceCtrlHandler start"));
    switch(CtrlCode){
        case SERVICE_CONTROL_STOP:
            OutputDebugString(_T("ServiceCtrlHnalder SERVICE_CONTROL_STOP Request"));
            if(g_ServiceStatus.dwCurrentState != SERVICE_RUNNING){ break; }

            g_ServiceStatus.dwControlsAccepted = 0;
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            g_ServiceStatus.dwWin32ExitCode = 0;
            g_ServiceStatus.dwCheckPoint = 4;

            if(SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE){
                OutputDebugString(_T("ServiceCtrlHandler SetService Status ERROR"));
            }
            SetEvent(g_ServiceStopEvent);
            break;
        default:
            break;
    }
    OutputDebugString(_T("ServiceCtrlHandler end"));
}

HANDLE DuplicateTokenForConsoleSession(){
    auto console_session_id = WTSGetActiveConsoleSessionId();
    if(console_session_id == 0xFFFFFFFF){
        return NULL;
    }

    HANDLE current_token;
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &current_token)){
        return NULL;
    }

    HANDLE new_token;
    if(!DuplicateTokenEx(current_token, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &new_token)){
        CloseHandle(current_token);
        return NULL;
    }

    CloseHandle(current_token);

    if(!SetTokenInformation(new_token, TokenSessionId, &console_session_id, sizeof(console_session_id))){
        CloseHandle(new_token);
        return NULL;
    }

    return new_token;
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam){
    OutputDebugString(_T("Worker thread start"));

    OutputDebugStringW(L"Starting child sunshine processes now...");
    WCHAR machineName[1024];
    DWORD machineNameSize = 1024;
    GetComputerNameW(machineName, &machineNameSize);

    HANDLE htoken = DuplicateTokenForConsoleSession();

    while(WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0){
        OutputDebugString(_T("Worker Loop"));

        if(g_DisplayNames.size() == 0){
            OutputDebugString(_T("Getting display names"));
            GetDisplayNames();
        }else if(sunshineInstances.size() != g_DisplayNames.size()){
            OutputDebugStringW(L"creating sunshine instance info objects");
            sunshineInstances.resize(g_DisplayNames.size());

            for(int i = 0; i < g_DisplayNames.size(); i++){
                WCHAR sunshineLogName[1024];
                _snwprintf(sunshineLogName, 1024, L"sunshine_log_screen_%d.log", i);
                HANDLE logFile = OpenLogFileHandle(sunshineLogName);

                SunshineInstance instanceInfo{};
                WCHAR appdata[MAX_PATH];
                GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);

                _snwprintf(
                    instanceInfo.cmd_args, 2048,
                    L"\"%ls\\sunshine_%d.conf\" output_name=\"\\\\.\\%ls\" port=%d file_state=\"%ls\\sunshineTemp%d.json\" sunshine_name=%ls_screen_%d",
                    appdata,
                    i,
                    g_DisplayNames[i].c_str(),
                    40000 + i*100,
                    appdata,
                    i,
                    machineName,
                    i
                );

                OutputDebugStringW(instanceInfo.cmd_args);
                ZeroMemory(&instanceInfo.pi, sizeof(PROCESS_INFORMATION));
                ZeroMemory(&instanceInfo.si, sizeof(STARTUPINFOW));

                instanceInfo.si.cb = sizeof(STARTUPINFOW);
                instanceInfo.si.hStdInput = NULL;
                instanceInfo.si.hStdOutput = logFile;
                instanceInfo.si.hStdError = logFile;
                instanceInfo.si.dwFlags = STARTF_USESTDHANDLES;
                instanceInfo.si.lpDesktop = L"winsta0\\default";

                sunshineInstances[i] = instanceInfo;

                BOOL bSuccess = CreateProcessAsUserW(
                    htoken,
                    L"sunshine.exe",
                    sunshineInstances[i].cmd_args,
                    NULL,
                    NULL,
                    TRUE,
                    CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                    NULL,
                    NULL,
                    &instanceInfo.si,
                    &instanceInfo.pi
                );

                if(!bSuccess){
                    OutputDebugStringW(L"Failed to Create sunshine Process");
                    CloseHandle(sunshineInstances[i].pi.hProcess);
                }else{
                    OutputDebugStringW(L"Created sunshine process");
                    Sleep(1000);
                }

                sunshineInstances[i] = instanceInfo;
            }
        } else {
            OutputDebugStringW(L"Checking sunshine processes:");
            vector<HANDLE> handles;
            for(int i = 0; i < sunshineInstances.size(); i++){
                handles.push_back(sunshineInstances[i].pi.hProcess);
            }

            DWORD result = WaitForMultipleObjects(handles.size(), handles.data(), FALSE, 100);
            OutputDebugStringW(L"WaitforMultipleObjects Result:");
            OutputDebugStringW(to_wstring(result).c_str());
            if(result == WAIT_FAILED){
                OutputDebugStringW(L"WAIT_FAILED ERROR. Error code:");
                OutputDebugStringW(to_wstring(GetLastError()).c_str());
            }else if(result == WAIT_TIMEOUT){
                OutputDebugStringW(L"Wait process timeout, checking again later..");
            }else if(result >= WAIT_OBJECT_0 && result < handles.size()){
                OutputDebugStringW(L"A process has exited. relaunching...");
                SunshineInstance info = sunshineInstances[result];
                BOOL bSuccess = CreateProcessAsUserW(
                    htoken,
                    L"sunshine.exe",
                    info.cmd_args,
                    NULL,
                    NULL,
                    TRUE,
                    CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                    NULL,
                    NULL,
                    &info.si,
                    &info.pi
                );

                if(bSuccess){
                    OutputDebugStringW(L"Successfully relaunced sunshine process");
                }else{
                    OutputDebugStringW(L"Failed to relaunch sunshine process");
                }

                sunshineInstances[result] = info;
            }
        }

        Sleep(5000);
    }
    CloseHandle(htoken);
    OutputDebugStringW(L"Serice Worker Thread STopping. Stopping sunshine.exe processes still running...");
    for(int i = 0; i < sunshineInstances.size(); i++){
        if(TerminateProcess(sunshineInstances[i].pi.hProcess, 1)){
            OutputDebugStringW(L"successfully terminated sunshine instance");
            CloseHandle(sunshineInstances[i].pi.hProcess);
        }
    }

    OutputDebugString(_T("Worker thread end"));
    return ERROR_SUCCESS;
}

void write_txt_file(string filename, string input){
    FILE* f = fopen(filename.c_str(), "a+");
    fprintf(f, "%s\n", input.c_str());
    fclose(f);
}

int _tmain(int argc, TCHAR* argv[]){
    OutputDebugString(_T("Sunshine Service Main Start"));

    WCHAR module_path[MAX_PATH];
    GetModuleFileNameW(NULL, module_path, _countof(module_path));
    for(auto i = 0; i < 2; i++){
        auto last_sep = wcsrchr(module_path, '\\');
        if(last_sep){
            *last_sep = 0;
        }
    }

    SetCurrentDirectoryW(module_path);
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION) ServiceMain},
        {NULL, NULL}
    };

    if(StartServiceCtrlDispatcher(ServiceTable) == FALSE){
        OutputDebugString(_T("Sunshine Service Table Start Error"));
        return GetLastError();
    }

    OutputDebugString(_T("Sunhine service main end"));
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* arg){
    DWORD Status = E_FAIL;
    HANDLE hThread = INVALID_HANDLE_VALUE;

    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if(g_StatusHandle == NULL){
        OutputDebugString(_T("Sunshine Service CtrlHandle Error"));
        goto EXIT;
    }

    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    if(SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE){
        OutputDebugString(_T("sunshine service status error"));
    }

    OutputDebugString(_T("sunshine service starting"));
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if(g_ServiceStopEvent == NULL){
        OutputDebugString(_T("sunshine service create event error"));
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        g_ServiceStatus.dwCheckPoint = 1;
        if(SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE){
            OutputDebugString(_T("sunshine service set service status error"));
        }
        goto EXIT;
    }

    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    if(SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE){
        OutputDebugString(_T("sunshine service set service status error"));
    }

    hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    OutputDebugString(_T("sunshine service waiting for worker thread to complete"));
    WaitForSingleObject(hThread, INFINITE);
    OutputDebugString(_T("sunshine service worker thread stop event signaled"));
    CloseHandle(g_ServiceStopEvent);

    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;
    if(SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE){
        OutputDebugString(_T("sunshine service set service status error"));
    }

    EXIT:
    OutputDebugString(_T("Sunshine Service main Exit"));
    return;
}