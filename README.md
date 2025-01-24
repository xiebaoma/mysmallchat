我用C++修改了开源项目https://github.com/antirez/smallchat.git

## 修改

1，重写了server，程序在cpp-smallchat-server.cpp

2，修改了chatlib.h

​	是在网上找的教程，添加了extern"C"，为了让C++文件和C文件能一起编译，但是没有成功

## 疑问

1，C/C++一起编译的问题

我修改了chatlib.h文件，输入：

```
g++ -o chat-server cpp-smallchat-server.cpp chatlib.c
```

报错，显示有几个函数没有定义。



2，我在编写isValidMessage方法的时候突然想起传进来的msg就是一个字符串还是经过封装的数据，如果就是一个字符串的话，是什么时候解封装的呢？

```
bool isValidMessage(const char *msg)
    {
        return true; // 通过所有检查认为消息有效
    }
```

