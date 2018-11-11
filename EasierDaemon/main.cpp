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
string ObtainErrorStr(DWORD err)
{
    char * buf = nullptr;
    DWORD dw = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        nullptr,
        err,
        0,
        (LPSTR)&buf,
        256,
        nullptr
    );
    string sbuf = buf;
    LocalFree(buf);
    return std::move(sbuf);
}

// 获取在flag之后的命令行字符串
string GetCmdLineStrBehindFlag( CommandLineVars const & cmdVars, string const & flag )
{
    int inx = cmdVars.getFlagIndexInArgv(flag);
    if ( inx == -1 )
        return "";

    inx = inx + 1;

    int argc = cmdVars.getArgc() - inx;
    char const ** argv = cmdVars.getArgv() + inx;

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

// 启动进程到vector里
bool StartupProcessToVector(
    string & cmd,
    vector< SimpleHandle<HANDLE> > &processes,
    vector<HANDLE> &hProcesses,
    int iTargetPos
)
{
    HANDLE hProcess;
    if ( hProcess = StartupProcess(cmd) )
    {
        if ( iTargetPos == -1 )
        {
            processes.emplace_back(hProcess, nullptr, CloseHandle);
            hProcesses.emplace_back(hProcess);
        }
        else
        {
            processes[iTargetPos].attachNew(hProcess, nullptr, CloseHandle);
            hProcesses[iTargetPos] = hProcess;
        }
        return true;
    }
    else
    {
        return false;
    }
}

// 守护进程的业务逻辑
template < typename _Fx1, typename _Fx2, typename _Fx3, typename _Fx4, typename _Fx5 >
int DaemonMain(CommandLineVars const & cmdVars, bool * running, bool isServiceRunning, _Fx1 startupSuccess, _Fx2 startupFailed, _Fx3 restartupSuccess, _Fx4 restartupFailed, _Fx5 waitTimeout)
{
    size_t pc = cmdVars.getOption("--pc", 1); // 开启的目标进程数

    string cmd = GetCmdLineStrBehindFlag(cmdVars, "run");

    if ( isServiceRunning )
        OutputDebugStringA(( "Command: " + cmd ).c_str());
    else
        cout << "Command: " << ConsoleColor(fgYellow | bgBlue, cmd) << endl;

    vector< SimpleHandle<HANDLE> > processes; // 启动的子进程
    vector<HANDLE> hProcesses;

    for ( size_t i = 0; i < pc; i++ )
    {
        if ( StartupProcessToVector(cmd, processes, hProcesses, -1) )
        {
            startupSuccess(i);
        }
        else
        {
            startupFailed(i, ObtainErrorStr(GetLastError()));
            return -1;
        }
    }

    DWORD ret;
    while ( hProcesses.size() > 0 && *running )
    {
        ret = WaitForMultipleObjects(hProcesses.size(), hProcesses.data(), FALSE, 100);

        if ( ret == WAIT_FAILED )
        {
            break;
        }
        else if ( ret == WAIT_TIMEOUT )
        {
            waitTimeout();
        }
        else
        {
            size_t iPos = ret - WAIT_OBJECT_0;
            // 重启(dwRet - WAIT_OBJECT_0)的进程
            if ( StartupProcessToVector(cmd, processes, hProcesses, iPos) )
            {
                restartupSuccess(iPos);
            }
            else
            {
                restartupFailed(iPos, ObtainErrorStr(GetLastError()));
            }
        }
    } // while

    if ( isServiceRunning )
        OutputDebugStringA("Exit wait loop");
    else
        cout << "Exit wait loop\n";

    // 强制监控的进程退出
    for ( auto h : hProcesses )
    {
        BOOL b = TerminateProcess(h, 0);
        DWORD err = GetLastError();
        if ( isServiceRunning )
            OutputDebugStringA(ObtainErrorStr(err).c_str());
        else
            cerr << ConsoleColor(fgYellow, ObtainErrorStr(err));
    }
    WaitForMultipleObjects(hProcesses.size(), hProcesses.data(), TRUE, INFINITE);

    return 0;
}

// 服务相关
struct Service
{
public:
    SERVICE_STATUS_HANDLE _hServiceStatus;  // 服务状态句柄
    SERVICE_STATUS _servStatus;             // 服务状态结构
    bool _running;                          // 服务运行与否

    string _servname;                       // 服务名
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
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr
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

        //g_service.status(SERVICE_START_PENDING, NO_ERROR, 0, 2, 1000);

        //g_service.status(SERVICE_START_PENDING, NO_ERROR, 0, 3, 5000);

        g_service.status(SERVICE_RUNNING, NO_ERROR, 0, 0, 0);

        g_service._running = true;

        DaemonMain(
            cmdVars,
            &g_service._running,
            true,
            [] (int i) {
                OutputDebugStringA(Format("Launch child %d process!", i).c_str());
            },
            [] (int i, string errStr) {
                OutputDebugStringA(Format("%d: StartupProcessToVector() failed, %s", i, errStr.c_str()).c_str());
                g_service.status(SERVICE_STOPPED, NO_ERROR, 1, 0, 0);
            },
            [] (int iPos) {
                OutputDebugStringA(Format("Relaunch child %d process!", iPos).c_str());
            },
            [] (int iPos, string errStr) {
                OutputDebugStringA(Format("%d: StartupProcessToVector() failed, %s", iPos, errStr.c_str()).c_str());
            },
            [] () {
            }
        );

        // 标记服务退出
        g_service.status(SERVICE_STOPPED, NO_ERROR, 0, 0, 0);

    }

} g_service;

int main( int argc, char const ** argv )
{
    CommandLineVars cmdVars(argc, argv, "-displayname,-servname,-servdesc", "--pc", "install,uninstall,run,servrun");

    // edaemon install -servname "MyCmdDaemon" -servdesc "MyCmd Daemon" servrun --pc=1 run cmd.exe /c ping www.baidu.com
    // edaemon uninstall -servname "MyCmdDaemon"
    // edaemon --pc=1 run cmd.exe /c ping www.baidu.com

    // 服务安装 ---------------------------------------------------------------------------------
    if ( cmdVars.hasFlag("install") )
    {
        string rawServrun = GetCmdLineStrBehindFlag(cmdVars, "servrun");
        if ( rawServrun.empty() )
        {
            cerr << ConsoleColor(fgRed, "Service install failed, `servrun` is empty!") << endl;
            return 2;
        }

        string exePath = GetExecutablePath();
        string exeFile;
        FilePath(exePath, &exeFile);

        string servrun;
        string servname = cmdVars.getParam("-servname", exeFile);

        if ( HasSpace(exePath) )
            servrun = "\"" + exePath + "\" -servname " + servname + " " + rawServrun;
        else
            servrun = exePath + " -servname " + servname + " " + rawServrun;

        if ( !Service::Install(servname, servrun, cmdVars.getParam("-displayname"), cmdVars.getParam("-servdesc")) )
        {
            cerr << ConsoleColor(fgRed, "Service install failed!") << endl;
            return 2;
        }
        cout << "Service install success!\n";
    }
    // 服务卸载 ---------------------------------------------------------------------------------
    else if ( cmdVars.hasFlag("uninstall") )
    {
        string servname = cmdVars.getParam("-servname");

        if ( !servname.empty() )
        {
            if ( !Service::Uninstall(servname) )
            {
                cerr << ConsoleColor(fgRed, "Service uninstall failed!") << endl;
                return 3;
            }
            else
            {
                cout << "Service uninstall success!" << endl;
                return 0;
            }
        }
        return 3;
    }
    // 运行 -----------------------------------------------------------------------------------
    else if ( cmdVars.hasFlag("run") )
    {
        string servname = cmdVars.getParam("-servname");
        // 是否作为服务启动运行
        if ( !servname.empty() )
        {
            g_service.init(&cmdVars, servname);
            if ( g_service.startService() )
                return 0;
            else
                return 1;
        }

        // 直接运行

        bool running = true;

        DaemonMain(
            cmdVars,
            &running,
            false,
            [] (int i) {
                cout << Format("Launch child %d process!\n", i);
            },
            [] (int i, string errStr) {
                cerr << ConsoleColor(fgRed, Format("%d: StartupProcessToVector() failed, %s", i, errStr.c_str())) << endl;
            },
            [] (int iPos) {
                cout << Format("Relaunch child %d process!\n", iPos);
            },
            [] (int iPos, string errStr) {
                cerr << iPos << ConsoleColor(fgRed, Format("%d: StartupProcessToVector() failed, %s", iPos, errStr.c_str())) << endl;
            },
            [&running] () {
                if ( _kbhit() )
                {
                    wint_t ch = _getwch();
                    if ( ch == 27 )
                    {
                        running = false;
                    }
                    else
                    {
                        cout << ch << endl;
                    }
                }
            }
        );
    }
    else
    {
        cerr << ConsoleColor( fgRed, "Unknown cmd operate!" ) << endl;
    }

    return 0;
}

