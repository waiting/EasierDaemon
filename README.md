# EasierDaemon
This is a simple daemon on Windows that can monitor a program to maintain a certain number of process instances. It can also be installed as a Windows service.

Build:
You need the latest vs2017.

Example:
I want to keep 4 ping processes pinging Google all the time.

    edaemon.exe --pc=4 run cmd.exe /c ping www.google.com

Install service:

    edaemon.exe install -servname "MyPingGoogle" -servdesc "pinging Google all the time" servrun --pc=4 run cmd.exe /c ping www.google.com

Uninstall service:

    edaemon.exe uninstall -servname "MyPingGoogle"

