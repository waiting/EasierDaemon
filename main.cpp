#include <iostream>
#include <string>
#include <conio.h>
#include <winux.hpp>
using namespace winux;
using namespace std;

inline static bool IsSpace(char ch)
{
    return ch > '\0' && ch <= ' ';
}

inline static bool HasSpace( string const & str )
{
    for ( string::const_iterator it = str.begin(); it != str.end(); it++ )
        if ( IsSpace(*it) ) return true;
    return false;
}

// Windows错误代码转错误串
SimpleHandle<char*> GetErrorStr(DWORD err)
{
    char * buf = NULL;
    DWORD dw = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        NULL,
        err,
        0,
        (LPSTR)&buf,
        256,
        NULL
    );

    return winux::SimpleHandle<char *>(buf, NULL, LocalFree);
}

// 组成命令行字符串
string AssembleCommandLine( int argc, char const ** argv )
{
    string cmd;
    for ( int i = 0; i < argc; i++ )
    {
        if ( i != 0 ) cmd += " ";

        if ( HasSpace(argv[i]) )
        {
            cmd += "\"";
            cmd += argv[i];
            cmd += "\"";
        }
        else
        {
            cmd += argv[i];
        }
    }
    return cmd;
}

// 启动进程
HANDLE StartupProcess(string & cmd, HANDLE * phMainThread = nullptr, HANDLE hStdin = INVALID_HANDLE_VALUE, HANDLE hStdout = INVALID_HANDLE_VALUE, HANDLE hStderr = INVALID_HANDLE_VALUE)
{
    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFOA si = { sizeof(si) };
    GetStartupInfoA(&si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = hStdin;
    si.hStdOutput = hStdout;
    si.hStdError = hStderr;

    BOOL bRet = CreateProcessA(nullptr, ( cmd.empty() ? "" : &cmd[0] ), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if ( !bRet )
    {
        return nullptr;
    }

    if ( phMainThread != nullptr )
        *phMainThread = pi.hThread;
    else
        CloseHandle(pi.hThread);

    return pi.hProcess;
}

struct Service
{
public:
    SERVICE_STATUS_HANDLE _hServiceStatus;  // 服务状态句柄
    SERVICE_STATUS _servStatus;             // 服务状态结构
    string _servname;                       // 服务名
    bool _running;                          // 服务运行与否

    CommandLineVars * _pCmdVars;
public:
    Service()
    {
        _hServiceStatus = nullptr;
        memset(&_servStatus, 0, sizeof(_servStatus));
        _pCmdVars = nullptr;

        _servStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        _running = false;
    }

    // 初始化
    void init(CommandLineVars * pCmdVars, string const & servname)
    {
        _pCmdVars = pCmdVars;
        _servname = servname;
    }

    // 启动服务控制调度
    bool startService()
    {
        string servname = _servname;
        SERVICE_TABLE_ENTRY servTable[2] = { { &servname[0], Service::ServiceMain }, { nullptr, nullptr } };

        if ( !StartServiceCtrlDispatcher(servTable) )
        {
            cerr << ConsoleColor(fgRed, "StartServiceCtrlDispatcher() failed!") << endl;
            return false;
        }
        else
        {
            return true;
        }
    }

    void status( DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint )
    {
        _servStatus.dwCurrentState = dwCurrentState;
        if ( dwCurrentState == SERVICE_START_PENDING )
        {
            _servStatus.dwControlsAccepted = 0;
        }
        else
        {
            _servStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP /*| SERVICE_ACCEPT_PAUSE_CONTINUE*/ | SERVICE_ACCEPT_SHUTDOWN;
        }

        if ( dwServiceSpecificExitCode == 0 )
        {
            _servStatus.dwWin32ExitCode = dwWin32ExitCode;
        }
        else
        {
            _servStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        }

        _servStatus.dwServiceSpecificExitCode = dwServiceSpecificExitCode;
        _servStatus.dwCheckPoint = dwCheckPoint;
        _servStatus.dwWaitHint = dwWaitHint;

        SetServiceStatus(_hServiceStatus, &_servStatus);

    }

    bool registerCtrlHandler(LPHANDLER_FUNCTION lpHandlerProc)
    {
        _hServiceStatus = RegisterServiceCtrlHandlerA(_servname.c_str(), ServiceCtrlHandler);
        return _hServiceStatus != nullptr;
    }

    // 判断服务是否安装
    static bool IsInstalled(string const & servname)
    {
        SimpleHandle<SC_HANDLE> scmHandle;     // 服务控制管理器句柄
        SimpleHandle<SC_HANDLE> serviceHandle; // 服务句柄
        scmHandle.attachNew(OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS), nullptr, CloseServiceHandle);
        if ( scmHandle )
        {
            serviceHandle.attachNew(OpenService(scmHandle.get(), servname.c_str(), SERVICE_QUERY_CONFIG), nullptr, CloseServiceHandle);
            if ( serviceHandle.get() )
            {
                return true;
            }
        }
        return false;
    }

    static bool Install(string servname, string servrun, string displayname = "", string servdesc = "")
    {
        if ( IsInstalled(servname) )
            return true;

        if ( displayname.empty() ) displayname = servname;
        if ( servdesc.empty() ) servdesc = displayname;

        SERVICE_DESCRIPTION sd = { ( servdesc.empty() ? "" : &servdesc[0] ) };
        SimpleHandle<SC_HANDLE> scmHandle;     // 服务控制管理器句柄
        SimpleHandle<SC_HANDLE> serviceHandle; // 服务句柄

        scmHandle.attachNew(OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS), nullptr, CloseServiceHandle);

        if ( !scmHandle )
        {
            cerr << ConsoleColor(fgRed, "Couldn't open service manager") << endl;
            return false;
        }

        serviceHandle.attachNew(CreateService(
            scmHandle.get(),
            servname.c_str(),
            displayname.c_str(),
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_DEMAND_START,
            SERVICE_ERROR_NORMAL,
            servrun.c_str(),
            NULL,
            NULL,
            NULL,
            NULL,
            NULL
        ), nullptr, CloseServiceHandle);

        if ( !serviceHandle )
        {
            cerr << ConsoleColor(fgRed, "Couldn't create service") << endl;
            return false;
        }

        ChangeServiceConfig2A(serviceHandle.get(), SERVICE_CONFIG_DESCRIPTION, &sd);

        return true;
    }

    static bool Uninstall(string const & servname)
    {
        SimpleHandle<SC_HANDLE> scmHandle;     // 服务控制管理器句柄
        SimpleHandle<SC_HANDLE> serviceHandle; // 服务句柄
        SERVICE_STATUS status;

        if ( !IsInstalled(servname) )
            return true;

        scmHandle.attachNew(OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS), nullptr, CloseServiceHandle);

        if ( !scmHandle )
        {
            cerr << ConsoleColor(fgRed, "Couldn't open service manager") << endl;
            return false;
        }

        serviceHandle.attachNew(OpenServiceA(scmHandle.get(), servname.c_str(), SERVICE_STOP | DELETE), nullptr, CloseServiceHandle);

        if ( !serviceHandle )
        {
            cerr << ConsoleColor(fgRed, "Couldn't open service") << endl;
            return false;
        }

        ControlService( serviceHandle.get(), SERVICE_CONTROL_STOP, &status);

        if ( DeleteService(serviceHandle.get()) )
            return true;

        cerr << ConsoleColor(fgRed, "Service could not be deleted") << endl;
        return false;
    }

private:
    // 服务控制响应函数
    static void CALLBACK ServiceCtrlHandler(DWORD ControlCode)
    {
        switch ( ControlCode )
        {
        case SERVICE_CONTROL_STOP:
            g_service.status(SERVICE_STOP_PENDING, NO_ERROR, 0, 1, 5000);
            g_service._running = false;
            //StopWorkThread();
            break;
        case SERVICE_CONTROL_PAUSE:
            //if ( g_fRunning != 0 && g_fPaused == 0 )
        {
            g_service.status(SERVICE_PAUSE_PENDING, NO_ERROR, 0, 1, 1000);
            //PauseWorkThread();
            g_service.status(SERVICE_PAUSED, NO_ERROR, 0, 0, 0);
        }
        break;
        case SERVICE_CONTROL_CONTINUE:
            //if ( g_fRunning != 0 && g_fPaused != 0 )
        {
            g_service.status(SERVICE_CONTINUE_PENDING, NO_ERROR, 0, 1, 1000);
            //ResumeWorkThread();
            g_service.status(SERVICE_RUNNING, NO_ERROR, 0, 0, 0);
        }
        break;
        case SERVICE_CONTROL_INTERROGATE:
            break;
        case SERVICE_CONTROL_SHUTDOWN:
            break;

        }
    }

    // NT服务Main函数
    static void CALLBACK ServiceMain(DWORD argc1, LPSTR* argv1)
    {
        if ( !g_service.registerCtrlHandler(ServiceCtrlHandler) )
            return;

        CommandLineVars &cmdVars = *g_service._pCmdVars;

        g_service.status(SERVICE_START_PENDING, NO_ERROR, 0, 1, 5000);

        g_service.status(SERVICE_START_PENDING, NO_ERROR, 0, 2, 1000);

        g_service.status(SERVICE_START_PENDING, NO_ERROR, 0, 3, 5000);

        g_service.status(SERVICE_RUNNING, NO_ERROR, 0, 0, 0);

        g_service._running = true;
        size_t pc = cmdVars.getOption("--pc", 1); // 开启的目标进程数

        int startIndex = cmdVars.getFlagIndexInArgv("run") + 1;
        string cmd = AssembleCommandLine(cmdVars.getArgc() - startIndex, cmdVars.getArgv() + startIndex);

        //cout << "Command: " << ConsoleColor(fgYellow | bgBlue, cmd) << endl;
        OutputDebugStringA(Format("Command:%s,argc=%d,run start=%d", cmd.c_str(), cmdVars.getArgc(), startIndex).c_str());

        vector< SimpleHandle<HANDLE> > processes; // 启动的子进程
        vector<HANDLE> hProcesses;

        for ( size_t i = 0; i < pc; i++ )
        {
            HANDLE hProcess;
            if ( hProcess = StartupProcess(cmd) )
            {
                processes.emplace_back(hProcess, nullptr, CloseHandle);
                hProcesses.emplace_back(hProcess);
                //cout << Format("Launch child %d process!\n", i);
                OutputDebugStringA(Format("Launch child %d process!", i).c_str());
            }
            else
            {
                auto pch = GetErrorStr(GetLastError());
                string errStr = pch.get();
                //cerr << ConsoleColor(fgRed, "CreateProcess() failed, " + errStr) << endl;
                OutputDebugStringA(( "CreateProcess() failed, " + errStr ).c_str());
                g_service.status(SERVICE_STOPPED, NO_ERROR, 1, 0, 0);
                return;
            }
        }

        bool relaunch = true; // 重启子进程
        g_service._running = true;

        DWORD dwRet;
        while ( hProcesses.size() > 0 && g_service._running )
        {
            dwRet = WaitForMultipleObjects(hProcesses.size(), hProcesses.data(), FALSE, 1000);

            if ( dwRet == WAIT_FAILED )
            {
                break;
            }
            else if ( dwRet == WAIT_TIMEOUT )
            {
                if ( _kbhit() )
                {
                    wint_t ch = _getwch();
                    if ( ch == 27 )
                    {
                        g_service._running = false;
                    }
                    else
                    {
                        cout << ch << endl;
                    }
                }
            }
            else
            {
                size_t iPos = dwRet - WAIT_OBJECT_0;
                if ( !relaunch )
                {
                    // 删除
                    hProcesses.erase(hProcesses.begin() + iPos);
                    processes.erase(processes.begin() + iPos);
                }
                else
                {
                    // 重启(dwRet - WAIT_OBJECT_0)的进程
                    HANDLE hProcess;
                    if ( hProcess = StartupProcess(cmd) )
                    {
                        processes[iPos].attachNew(hProcess, nullptr, CloseHandle);
                        hProcesses[iPos] = hProcess;
                        //cout << Format("Relaunch child %d process!\n", iPos);
                        OutputDebugStringA(Format("Relaunch child %d process!", iPos).c_str());
                    }
                    else
                    {
                        auto pch = GetErrorStr(GetLastError());
                        string errStr = pch.get();
                        //cerr << iPos << ConsoleColor(fgRed, ": CreateProcess() failed, " + errStr) << endl;
                        OutputDebugStringA(Format("%d: CreateProcess() failed, %s", iPos, pch.get()).c_str());
                    }
                }
            }
        } // while

        //cout << "exit while\n";
        OutputDebugStringA("Exit while");
        for ( auto h : hProcesses )
        {
            BOOL b = TerminateProcess(h, 0);
            cout << GetErrorStr(GetLastError()).get();
        }
        WaitForMultipleObjects(hProcesses.size(), hProcesses.data(), TRUE, INFINITE);

        g_service.status(SERVICE_STOPPED, NO_ERROR, 0, 0, 0);

    }

} g_service;

int main( int argc, char const ** argv )
{
    CommandLineVars cmdVars(argc, argv, "-displayname,-servname,-servdesc", "--pc", "install,uninstall,run,servrun");

    // edaemon install -servname "MyCmdDaemon" -servdesc "MyCmd Daemon" servrun --pc=1 run cmd.exe /c ping www.baidu.com

    // edaemon uninstall -servname "MyCmdDaemon"

    // edaemon --pc=1 run mycmd param1 param2 param3 param4

    if ( cmdVars.hasFlag("install") )
    {
        int inx = cmdVars.getFlagIndexInArgv("servrun");
        if ( inx == -1 )
        {
            cerr << ConsoleColor(fgRed, "Service install failed, no `servrun`!") << endl;
            return 2;
        }

        inx = inx + 1;

        string exepath = GetExecutablePath();
        string exefile;
        FilePath(exepath, &exefile);
        string servrun, servname = cmdVars.getParam("-servname", exefile);

        if ( HasSpace(exepath) )
            servrun = "\"" + exepath + "\" -servname " + servname + " " + AssembleCommandLine(argc - inx, argv + inx);
        else
            servrun = exepath + " -servname " + servname + " " + AssembleCommandLine(argc - inx, argv + inx);

        if ( !Service::Install(servname, servrun, cmdVars.getParam("-displayname"), cmdVars.getParam("-servdesc")) )
        {
            return 2;
        }
        cout << "Service install success!\n";
    }
    else if ( cmdVars.hasFlag("uninstall") )
    {
        string servname = cmdVars.getParam("-servname");

        if ( !servname.empty() )
        {
            if ( Service::Uninstall(servname) )
            {
                cout << "Service uninstall success!" << endl;
                return 0;
            }
            else
            {
                cerr << ConsoleColor(fgRed, "Service uninstall failed!") << endl;
                return 3;
            }
        }
        return 3;
    }
    else if ( cmdVars.hasFlag("run") )
    {
        string servname = cmdVars.getParam("-servname");

        if ( !servname.empty() )
        {
            g_service.init(&cmdVars, servname);
            if ( g_service.startService() )
                return 0;
            else
                return 1;
        }

        size_t pc = cmdVars.getOption("--pc", 1); // 开启的目标进程数
        
        int startIndex = cmdVars.getFlagIndexInArgv("run") + 1;
        string cmd = AssembleCommandLine( argc - startIndex, argv + startIndex );

        cout << "Command: " << ConsoleColor(fgYellow | bgBlue, cmd) << endl;

        vector< SimpleHandle<HANDLE> > processes; // 启动的子进程
        vector<HANDLE> hProcesses;

        for ( size_t i = 0; i < pc; i++ )
        {
            HANDLE hProcess;
            if ( hProcess = StartupProcess(cmd) )
            {
                processes.emplace_back( hProcess, nullptr, CloseHandle );
                hProcesses.emplace_back(hProcess);
                cout << Format("Launch child %d process!\n", i);
            }
            else
            {
                auto pch = GetErrorStr(GetLastError());
                string errStr = pch.get();
                cerr << ConsoleColor(fgRed, "CreateProcess() failed, " + errStr ) << endl;
                return -1;
            }
        }
        bool relaunch = true; // 重启子进程
        bool stop = false;
        DWORD dwRet;
        while ( hProcesses.size() > 0 && !stop )
        {
            dwRet = WaitForMultipleObjects(hProcesses.size(), hProcesses.data(), FALSE, 100);

            if ( dwRet == WAIT_FAILED )
            {
                break;
            }
            else if ( dwRet == WAIT_TIMEOUT )
            {
                if ( _kbhit() )
                {
                    wint_t ch = _getwch();
                    if ( ch == 27 )
                    {
                        stop = true;
                    }
                    else
                    {
                        cout << ch << endl;
                    }
                }
            }
            else
            {
                size_t iPos = dwRet - WAIT_OBJECT_0;
                if ( !relaunch )
                {
                    // 删除
                    hProcesses.erase(hProcesses.begin() + iPos);
                    processes.erase(processes.begin() + iPos);
                }
                else
                {
                    // 重启(dwRet - WAIT_OBJECT_0)的进程
                    HANDLE hProcess;
                    if ( hProcess = StartupProcess(cmd) )
                    {
                        processes[iPos].attachNew(hProcess, nullptr, CloseHandle);
                        hProcesses[iPos] = hProcess;
                        cout << Format("Relaunch child %d process!\n", iPos);
                    }
                    else
                    {
                        auto pch = GetErrorStr(GetLastError());
                        string errStr = pch.get();
                        cerr << iPos << ConsoleColor(fgRed, ": CreateProcess() failed, " + errStr) << endl;
                    }
                }
            }
        } // while

        //cout << "exit while\n";
        for ( auto h : hProcesses )
        {
            BOOL b = TerminateProcess(h, 0);
            cout << GetErrorStr(GetLastError()).get();
        }
        WaitForMultipleObjects(hProcesses.size(), hProcesses.data(), TRUE, INFINITE);
    }
    else
    {
        cerr << ConsoleColor( fgRed, "Unknown operate!" ) << endl;
    }


    return 0;
}
