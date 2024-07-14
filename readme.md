# 文本编辑器
by JJ
email:1468349372@qq.com
reference: https://viewsourcecode.org/snaptoken/kilo/index.html

## 1.项目概述
开发一个在linux平台上可以运行的文本编辑器，完全用c语言实现，可以在该程序内打开文件或新建文件进行编辑

## 2.环境设置

- 操作系统：Ubuntu
- 编辑器 ：vim
- 编译工具：gcc / Makefile

## 3.项目结构
### includes 
包含头文件
### defines 
定义常量
- CTRL_KEY 在ascii码层面，通过将高位设置为0，将a～z变成1～26（ctrl-a ~ z）
- editorKey 特殊建的映射值（特殊建有多个字节，通过editorReadKey函数可以将特殊建映射为这里设置的整数值）
### data  
定义全局变量
- erow_t 存储一个文本行
    - size,chars : 实际的文本及其大小
    - rsize,render : 渲染到屏幕上的文本及其大小，与前者的区别在于对tab键的处理
- editorConfig  存储编辑器参数，如屏幕大小，文件行数等
### prototypes 
函数的声明，主要是这些函数的调用在定义的前面
### terminal 
控制终端
- die 关闭
- disableRawMode 关闭原始模式
- enableRawMode 打开原始模式
- editorReadKey 从终端接收输入字符串，返回设置好的常量整数或者字符对应的ascii码值
- getWindowSize 设置全局变量中的行数和列数为屏幕的行列数
### row operations
有关文本行操作
- editorRowCx2Rx 将chars索引(实际输入的字符)转换render索引（输出到屏幕上的索引）
- editorRowRx2Cx 将render索引转化为chars索引
- editorUpdateRow 将chars字符串转化为render字符串
- editorInsertRow 插入一个新的文本行
- editorFreeRow 释放一个文本行
- editorDelRow 删除一个文本行
- editorRowInsertChar 插入一个字符
- editorRowAppendString 在行的末尾插入一个字符串
- editorRowDelChar 删除一个字符

### editor operation
编辑器层面的操作，比文本行操作更接近用户层，调用时不需要考虑行/列坐标等底层细节
- editorInsertChar 插入字符
- editorInsertNewline 插入新行
- editorDelChar 删除字符
### file io
控制文件的输入输出
- editorRows2String 设置屏幕上文本的总长度，将屏幕上的文本和成为一个长字符串
- editorOpen 打开指定文件并逐行将文本渲染到编辑器屏幕上
- editorSave 保存文件
### find
查找字符串
- editorFind 查找字符串，将当前光标移动到该字符串处，然后将该行置顶
### append buffer
在该程序的多处都有写入（write）操作，每次操作大致相同，但是分散的write指令可能会在某个write函数处发现错误,不确定性较大。添加缓冲区通过将所有的输出转化为字符串添加用来集成写入操作
- abf_t 数据结构，字符串类型
- abAppend 字符串添加
- abFree 释放
### output
将信息输出到屏幕上
- editorSetStatusMessage 输出状态栏信息
- editorScroll 滚动显示文本信息
- editorDrawRows 诸行渲染信息
- editorDrawStatusBar 渲染状态栏
- editorDrawMessageBar 渲染信息栏
- editorRefreshScreen 刷新屏幕，会在main函数的循环中调用，会不断地根据相关参数更新屏幕上的渲染信息

### input
接受键盘的输入
- editorPrompt 从屏幕底部接受命令
- editorMoveCursor 改变当前光标位置
- editorProcessKeypress 处理输入的字符串，主要是对特殊字符进行处理，有些字符一次性会输入多个字节，将这多个字节解析成整数类型，能够在后续的处理中更方便的根据输入选择正确的操作


### init
- initEditor 初始化各种参数，主要是全局变量editorConfig E
- main 主函数 打开原始模式，初始化编辑器，进入输出-输入-处理循环

## 4.主要功能

### 文件操作

- 文件打开
    - 带参数运行程序会打开目标文件
    - 不带参数打开程序会打开一个空白文件
- 文件保存
    - 若打开以存在的文件，保存在原位置
    - 若不打开已存在的文件，保存在指定位置
        
### 编辑器操作
- 上下左右箭头
    - 上下左右移动
    - 左右在文本行（非首行）首会退到上一行，在文本行尾会到下一行
    - 上下在遇到更短的文本行时，会退到行尾
- 插入字符
    - 按下正常字符会在当前光标位置插入字符，光标会后移
    - backspace会删除当前光标前一个字符,光标前移。在行首退到上一行
    - delete 删除当前光标字符，光标不移动。在行尾将下一行连接到本行
    - enter 会开启一个新的行
- ctrl-s
    - 文件保存
- ctrl-q
    - 若文件未被更改，直接退出
    - 若文件已被更改，提醒保存
- ctrl-f
    - 在最后一行输入字符串，目标字符串会出现在屏幕顶端，光标移动到该词
### 文本显示
- 打开空白文件后在屏幕中间显示信息，有输入有会消失
- 打开文件后，若屏幕上有空行，会在行首绘制波浪线
- 打开文件后会在下方倒数第二行显示一个白框，显示文件信息：文件名，总行数，当前光标所在的行数        
- 打开文件后在最后一行显示帮助信息：Ctrl- s 保存 | Ctrl - q 退出 | Ctrl - f 查找字符串

## 5.参考资料
参考链接：https://viewsourcecode.org/snaptoken/kilo/index.html
- done step -- 1 - 135
- skip step -- 90 - 91

## 6.更进一步
- 解析某种格式的文件，能够进行相应的不同颜色的字符渲染
- 创建新行时自动匹配缩进