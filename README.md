# EasierDaemon
这是一个Windows上简易的守护程序，可以监视给定的程序保持一定的进程实例数，可安装为Windows服务。

使用示例：
我想保持4个ping命令进程不停的ping百度

安装服务

    edaemon.exe install -servname "PingBaidu" -servdesc "不断的ping baidu.com" servrun --pc=4 run cmd.exe /c ping www.baidu.com

卸载服务

    edaemon.exe uninstall -servname "PingBaidu"
