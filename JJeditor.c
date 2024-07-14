/************************************************************************************
        > File Name: ./texteditor/JJeditor.c
        > Author: jj
        > Mail: 1468349372@qq.com
        > Created Time: Mon 08 Apr 2024 03:33:26 AM PDT
 ***********************************************************************************/

/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include<time.h>
#include<string.h>
#include<errno.h>
#include<ctype.h>
#include<stdio.h>
#include<termios.h>
#include<unistd.h>
#include<stdlib.h>
#include<stdarg.h>
#include<sys/ioctl.h>
#include<sys/types.h>
#include<fcntl.h>


/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define JJEDITOR_VERSION "1.0.0"
#define TAB_SIZE    8 
#define QUIT_TIMES  3



enum editorKey{
    BACKSPACE = 127,
    ARROW_LEFT =1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};


/*** data ***/

typedef struct _erow_t{
    int size;
    char * chars;

    int rsize;
    char* render;//为那行文本绘制的实际字符
}erow_t;


typedef struct editorConfig{
    struct termios orig_termios;
    
    int screenrows;
    int screencols;

    int cx,cy;
    int rx;

    int numrows;
    erow_t*  row;
    int dirty;
    
    char* filename;
    int rowoff; //跟踪用户当前滚动到文件的哪一行
    int coloff;

    char statusmsg[80];
    time_t statusmsg_time;
}editorConfig;

editorConfig E;


/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);

void editorRefreshScreen();
char * editorPrompt(char* prompt);
/*** terminal***/

void die(const char*s){
    write(STDOUT_FILENO,"\x1b[2J",4);
    write(STDOUT_FILENO,"\x1b[H",3);

    perror(s);
    exit(1);
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO,&E.orig_termios) == -1) die("hi jiji! tcgetattr ?");
    atexit(disableRawMode);
    
    struct termios raw = E.orig_termios;

    raw.c_iflag &= ~(IXON | ICRNL |BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN| ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw) == -1) die("tcsetattr");
}
int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}


int getWindowSize(int *rows,int *cols){
    struct winsize ws;

    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws) == -1 || ws.ws_col == 0){
        return -1;
    }else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

int editorRowCx2Rx(erow_t *row , int cx){
    int rx = 0;
    
    int j;
    for(j = 0;j<cx;j++){
        if(row->chars[j] == '\t'){
            rx+=(TAB_SIZE - 1) - (rx % TAB_SIZE);
        }

        rx++;
    }

    
    return rx;
}   

int editorRowRx2Cx(erow_t *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (TAB_SIZE - 1) - (cur_rx %TAB_SIZE);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}

void editorUpdateRow(erow_t * row){
    int tabs = 0;
    int j;
    for(j = 0; j < row->size;j++){
        if(row->chars[j] == '\t'){
            tabs++;
        }
    }


    free(row->render);
    row->render = malloc(row->size + 1 + tabs * (TAB_SIZE - 1));

    int idx = 0;
    for(j = 0;j< row->size;j++){
        if(row->chars[j] == '\t'){
            row->render[idx++] = ' ';
            
            while(idx % TAB_SIZE != 0){
                row->render[idx++] = ' ';
            }
        }else{
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;

}


void editorInsertRow(int at,char *s,size_t len){
    if(at < 0 || at > E.numrows) return;

    E.row = realloc(E.row,sizeof(erow_t)*(E.numrows + 1));
    memmove(&E.row[at+1],&E.row[at],sizeof(erow_t) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len  + 1);
    memcpy(E.row[at].chars,s,len);
    E.row[at].chars[len] = '\0';
    
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}   

void editorFreeRow(erow_t * row){
    free(row->render);
    free(row->chars);
}

void editorDelRow(int at){
    if(at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at],&E.row[at+1],sizeof(erow_t)*(E.numrows -at -1));
    E.numrows --;
    E.dirty++;
}


void editorRowInsertChar(erow_t * row,int at,int c){
    if(at < 0 || at > row->size) at = row->size;

    row->chars = realloc(row->chars,row->size + 2);

    memmove(&row->chars[at + 1],&row->chars[at],row->size - at + 1);

    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow_t * row,char *s,size_t len){
    row->chars = realloc(row->chars,row->size + len + 1);
    memcpy(&row->chars[row->size],s,len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow_t * row , int at){
    if( at < 0 || at >= row->size) return;
    memmove(&row->chars[at],&row->chars[at + 1],row->size - at);
    row->size --;
    editorUpdateRow(row);
    E.dirty++;
}

/*** editor operation ***/

void editorInsertChar(int c){
    if(E.cy == E.numrows){
        editorInsertRow(E.numrows,"",0);
    }

    editorRowInsertChar(&E.row[E.cy],E.cx,c);
    E.cx++;
}

void editorInsertNewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow_t *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    //editorInsertRow() 调用了 realloc() ，这可能会移动内存并使指针无效，这是使用全局变量的坏处
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar(){
    if(E.cy == E.numrows){
        editorInsertRow(E.numrows,"",0);
    }
    if(E.cx == 0 && E.cy == 0) return;


    erow_t *row = &E.row[E.cy];
    if(E.cx > 0){
        editorRowDelChar(row,E.cx-1);
        E.cx--;
    }else{
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy-1],row->chars,row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}


/*** file io ***/

char* editorRows2String(int * buflen){
    int totlen = 0;
    int j =0;
    for(j = 0;j < E.numrows;j++){
       totlen += E.row[j].size +1;
    }
    *buflen = totlen;

    char * buf = malloc(totlen);
    char* p = buf;
    for(j = 0;j < E.numrows;j++){
        memcpy(p,E.row[j].chars,E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(char * filename){
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename,"r");
    if(!fp) die("fopen");

    char*line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while((linelen = getline(&line,&linecap,fp)) != -1){//逐行读取文本文件
        while(linelen > 0 && (line[linelen -1] == '\n' || line[linelen -1] == '\r'))
            linelen--;//去除每行末尾的换行符号或回车符号
        editorInsertRow(E.numrows,line ,linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave(){
    if(E.filename == NULL){
        E.filename = editorPrompt("Save as(enter esc to abort) : %s");
        if(E.filename == NULL){
            editorSetStatusMessage("Save aborted");
            return;
        }
    }

    int len;
    char* buf = editorRows2String(&len);

    int fd = open(E.filename,O_RDWR | O_CREAT,0644);
    if(fd != -1){
    //    函数调整文件大小，使其与编辑器中的内容长度相同
       if(ftruncate(fd,len) != -1){
            if(write(fd,buf,len) == len){//向文件写入数据
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk",len);
                return;
            }
        }
       close(fd);
    }
//如果任何一个操作失败，文件描述符被关闭，编辑器内容缓冲区被释放，并且在状态栏显示保存失败的消息，包括相应的错误信息。
    free(buf);
    editorSetStatusMessage("Can`t save! I/O error : %s",strerror(errno));
}

/*** find ***/
void editorFind(){
    char* query = editorPrompt("Search (ESC to cancel): %s");
    if(!query) return;

    for(int i = 0;i<E.numrows;i++){
        erow_t * row = &E.row[i];
        char* match = strstr(row->render,query);
        if(match){
            E.cy = i;
            E.cx = editorRowRx2Cx(row , match - row->render);
            E.rowoff = E.numrows;//rowoff一定大于 cy 故在 scroll 函数中 if(E.cy < E.rowoff)成立，执行E.rowoff = E.cy
            //故该cy一定在屏幕的最顶端
            break;
        }
    }

    free(query);
}

/*** append buffer ***/

typedef struct abf_t{
    char *b;
    int len;
}abf_t;

#define ABF_INIT {NULL,0}

void abAppend(abf_t *ab,const char *s,int len){
    char * new = realloc(ab->b,ab->len + len);
    
    if(new == NULL) return;

    memcpy(&new[ab->len],s,len);
    ab->b = new;
    ab->len += len;
}

void abFree(abf_t *ab){
    free(ab->b);
}


/*** output ***/

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}


void editorScroll(){
    E.rx = 0;
    if(E.cy < E.numrows){
        E.rx = editorRowCx2Rx(&E.row[E.cy],E.cx);
    }

    if(E.cy < E.rowoff){
        E.rowoff = E.cy;
    }
    if(E.cy >= E.rowoff + E.screenrows){
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if(E.rx < E.coloff){
        E.coloff = E.rx;
    }
    if(E.rx >= E.coloff + E.screencols){
        E.coloff = E.rx - E.screencols + 1;
    }
}


void editorDrawRows(abf_t *ab){
    int y;
    for(y = 0; y < E.screenrows;y++){
        int filerow = y + E.rowoff;
        if(filerow >= E.numrows){
                if(E.numrows == 0 && y == E.screenrows /3){
                    char welcome[80];
                    int welcomelen = snprintf(welcome,sizeof(welcome),"JJ editor -- VERSION %s",
                        JJEDITOR_VERSION);
                    if(welcomelen > E.screencols) welcomelen = E.screencols;
                    int padding = (E.screencols - welcomelen)/2;
                    if(padding){
                        abAppend(ab,"~",1);
                        padding--;
                    }
                    while(padding--) abAppend(ab," ",1);
                    abAppend(ab,welcome,welcomelen);
                }else{

                    abAppend(ab,"~",1);
                }
        }else{
            int len = E.row[filerow].rsize - E.coloff;
            if(len < 0) len = 0;
            if(len > E.screencols) len = E.screencols;
            abAppend(ab,&E.row[filerow].render[E.coloff],len);

        }


        abAppend(ab,"\x1b[K",3);

        abAppend(ab,"\r\n",2);
    }
}

void editorDrawStatusBar(abf_t * ab){
    abAppend(ab,"\x1b[7m",4);

    char status[80],rstatus[80];
    int len = snprintf(status,sizeof(status),"%.20s - %d lines %s",E.filename ? E.filename : "[No Name",E.numrows,E.dirty? "(modified)":"");
    int rlen = snprintf(rstatus,sizeof(rstatus),"%d/%d",E.cy+1,E.numrows);

    if(len > E.screencols) len = E.screencols;
    abAppend(ab,status,len);

    while(len < E.screencols){
        if(E.screencols - len == rlen){
            abAppend(ab,rstatus,rlen);
            break;
        }else{
            abAppend(ab," ",1);
            len++;
        }
    }
    
    abAppend(ab,"\x1b[m",3);
    abAppend(ab,"\r\n",2);
}

void editorDrawMessageBar(abf_t * ab){
    abAppend(ab,"\x1b[K",3);
    int msglen = strlen(E.statusmsg);
    if(msglen > E.screencols) msglen = E.screencols;
    if(msglen && time(NULL) - E.statusmsg_time < 5) abAppend(ab,E.statusmsg,msglen);
}

void editorRefreshScreen(){
    editorScroll();

    abf_t ab = ABF_INIT;
    
    abAppend(&ab,"\x1b[?25l",6);
    //abAppend(&ab,"\x1b[2J",4);
    abAppend(&ab,"\x1b[H",3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];

    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",(E.cy - E.rowoff)+1,(E.rx- E.coloff) +1);
    abAppend(&ab,buf,strlen(buf));

    //abAppend(&ab,"\x1b[H",3);
    abAppend(&ab,"\x1b[?25h",6);

    write(STDOUT_FILENO,ab.b,ab.len);
    abFree(&ab);
}



/*** input ***/
char * editorPrompt(char* prompt){
    size_t buf_size = 128;
    char* buf =malloc(buf_size);

    size_t buf_len = 0;
    buf[buf_len] = '\0';

    while(1){
        editorSetStatusMessage(prompt,buf);//prompt期待有%s等格式化字符串
        editorRefreshScreen();
        
        int c = editorReadKey(); 
        if(c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE){
            if(buf_len != 0){
                buf[--buf_len] = '\0';
            }
        }else if(c == '\x1b'){//输入esc
            editorSetStatusMessage("");
            free(buf);
            return NULL;
        }else if(c == '\r'){
            if(buf_len != 0){
                editorSetStatusMessage("");
                return buf;
            }
        }else if(!iscntrl(c) && c < 128){
            if(buf_len == buf_size - 1){
                buf_size *= 2;
                buf = realloc(buf,buf_size);
            }

            buf[buf_len++] = c;
            buf[buf_len] = '\0';
        }

    }
}

void editorMoveCursor(int key){
    erow_t *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch(key){
        case ARROW_LEFT:
            if(E.cx !=  0){
                E.cx--;
            } else if(E.cy > 0){
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            
            break;
        case ARROW_RIGHT:
            if(row && E.cx <  row->size){
                E.cx++;
            } else if( row && E.cx == row->size){
                E.cx = 0;
                E.cy++;
            }
            break;
        case ARROW_UP:
            if(E.cy > 0){
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if(E.cy < E.numrows){
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if(E.cx > rowlen){
        E.cx = rowlen;
    }
}

void editorProcessKeypress() {
  static int quit_times = QUIT_TIMES;

  int c = editorReadKey();
  
  switch (c) {
    case '\r':
        editorInsertNewline();
        break;
    
    case CTRL_KEY('f'):
        editorFind();
        break;


    case CTRL_KEY('q'):
        if(E.dirty && quit_times > 0){
            editorSetStatusMessage("WARNING-File has unsaved changes."
                    "Press CTREL-Q %d more times to quit.",quit_times-- );
            return;
        }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;
    

    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
      E.cx = E.screencols - 1;
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if(c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

      
    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
    
    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default:
      editorInsertChar(c);
      break;
  }

  quit_times = QUIT_TIMES;

}



/*** init ***/

void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.numrows = 0;
    E.row = NULL;
    E.rowoff = 0;
    E.coloff = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.dirty = 0;

    if(getWindowSize(&E.screenrows,&E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;
}


int main(int argc , char * argv[]){
    enableRawMode();
    initEditor();
    if(argc >= 2){
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP : Ctrl-S = save | Ctrl-F = find | Ctrl-Q = quit");


    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
