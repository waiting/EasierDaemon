#include <iostream>
#include <winux.hpp>
using namespace winux;
using namespace std;

inline static bool IsSpace(char ch)
{
    return ch > '\0' && ch <= ' ';
}

inline static bool HasSpace( String const & str )
{
    for ( String::const_iterator it = str.begin(); it != str.end(); it++ )
        if ( IsSpace(*it) ) return true;
    return false;
}
// Windows错误代码转错误串
SimpleHandle<char*> GetErrorStr(int err)
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

int main( int argc, char const * argv[] )
{
    CommandLineVars cmdVars(argc, argv, "-servid,-servname,-servdesc", "--pc", "install,uninstall,run,servrun");

    // edaemon install -servid "MyCmdDaemon" -servdesc "MyCmd Daemon" --pc=1 servrun mycmd param1 param2 param3 param4

    // edaemon uninstall -servid "MyCmdDaemon"

    // edaemon --pc=1 run mycmd param1 param2 param3 param4

    if ( cmdVars.hasFlag("install") )
    {

    }
    else if ( cmdVars.hasFlag("uninstall") )
    {

    }
    else if ( cmdVars.hasFlag("run") )
    {
        size_t pc = cmdVars.getOption("--pc", 1); // 开启的目标进程数
        String cmd;
        int startIndex = cmdVars.getFlagIndexInArgv("run") + 1;
        for ( int i = startIndex; i < argc; i++ )
        {
            if ( i != startIndex ) cmd += " ";

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

        cout << "Command: " << ConsoleColor(fgYellow | bgBlue, cmd) << endl;

        vector< SimpleHandle<HANDLE> > processes; // 启动的子进程
        vector<HANDLE> hProcesses;

        STARTUPINFO si = { sizeof si };
        GetStartupInfoA(&si);

        si.dwFlags |= STARTF_USESTDHANDLES;

        //si.hStdOutput = nullptr;
        //si.hStdError = nullptr;
        //si.hStdInput = nullptr;

        for ( size_t i = 0; i < pc; i++ )
        {
            PROCESS_INFORMATION pi = { 0 };

            if ( CreateProcessA(nullptr, ( cmd.empty() ? "" : &cmd[0] ), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) )
            {
                CloseHandle(pi.hThread);
                processes.emplace_back( pi.hProcess, nullptr, CloseHandle );
                hProcesses.emplace_back(pi.hProcess);
                cout << Format("Launch child %d process!\n", i);
            }
            else
            {
                auto pch = GetErrorStr(GetLastError());
                String errStr = pch.get();
                cerr << ConsoleColor(fgRed, "CreateProcess() failed, " + errStr ) << endl;
                return -1;
            }
        }
        bool relaunch = true; // 重启子进程
        DWORD dwRet;
        while ( hProcesses.size() > 0 && ( dwRet = WaitForMultipleObjects(hProcesses.size(), hProcesses.data(), FALSE, INFINITE) ) != WAIT_FAILED )
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
                PROCESS_INFORMATION pi = { 0 };
                if ( CreateProcessA(nullptr, ( cmd.empty() ? "" : &cmd[0] ), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) )
                {
                    CloseHandle(pi.hThread);
                    processes[iPos].attachNew(pi.hProcess, nullptr, CloseHandle);
                    hProcesses[iPos] = pi.hProcess;
                    cout << Format("Relaunch child %d process!\n", iPos);
                }
                else
                {
                    auto pch = GetErrorStr(GetLastError());
                    String errStr = pch.get();
                    cerr << iPos << ConsoleColor(fgRed, ": CreateProcess() failed, " + errStr) << endl;
                }

            }
        }
    }
    else
    {
        cerr << ConsoleColor( fgRed, "Unknown operate!" ) << endl;
    }


    return 0;
}
