#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
char *readfile(const char *path, size_t *length) {
    FILE *f=fopen(path, "rb");
    if(!f)return NULL;
    if(fseek(f, 0, SEEK_END)) {
        fclose(f);
        return NULL;
    }
    long n=ftell(f);
    if(n<0||n>64*1024*1024) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *p=malloc((size_t)n+1);
    if(!p) {
        fclose(f);
        return NULL;
    }
    size_t got=fread(p, 1, (size_t)n, f);
    fclose(f);
    if(got!=(size_t)n) {
        free(p);
        return NULL;
    }
    p[n]=0;
    if(length)*length=(size_t)n;
    return p;
}
static void addtext(char **out, size_t *len, const char *s, size_t n) {
    char *p=realloc(*out, *len+n+1);
    if(!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    *out=p;
    memcpy(p+*len, s, n);
    *len+=n;
    p[*len]=0;
}
char *sourcefile(const char *path, int depth) {
    if(depth>16)return NULL;
    char *raw=readfile(path, NULL);
    if(!raw)return NULL;
    char *result=NULL;
    size_t length=0;
    for(char *s=raw;*s;) {
        char *end=strchr(s, '\n');
        size_t n=end?(size_t)(end-s):strlen(s);
        char *p=s;
        while(p<s+n&&isspace((unsigned char)*p))p++;
        if(n-(size_t)(p-s)>=8&&!strncmp(p, ".include", 8)&&isspace((unsigned char)p[8])) {
            p+=8;
            while(isspace((unsigned char)*p))p++;
            if(*p!='"') {
                free(raw);
                free(result);
                return NULL;
            }
            char *q=strchr(++p, '"');
            if(!q) {
                free(raw);
                free(result);
                return NULL;
            }
            char inc[4096];
            const char *slash=strrchr(path, '/');
            size_t dir=slash?(size_t)(slash-path+1):0;
            if(dir+(size_t)(q-p)>=sizeof inc) {
                free(raw);
                free(result);
                return NULL;
            }
            memcpy(inc, path, dir);
            memcpy(inc+dir, p, (size_t)(q-p));
            inc[dir+(size_t)(q-p)]=0;
            char *child=sourcefile(inc, depth+1);
            if(!child) {
                free(raw);
                free(result);
                return NULL;
            }
            addtext(&result, &length, child, strlen(child));
            addtext(&result, &length, "\n", 1);
            free(child);
        }
        else {
            addtext(&result, &length, s, n);
            addtext(&result, &length, "\n", 1);
        }
        if(!end)break;
        s=end+1;
    }
    free(raw);
    if(!result)result=strdup("");
    return result;
}
int compose_rom(const char *monitor, const char *basic, const char *native, char **out) {
    Symbol names[1024];
    int count=0;
    char *copy=strdup(basic);
    if(!copy)return 0;
    for(char *s=copy;s;) {
        char *next=strchr(s, '\n');
        if(next)*next++=0;
        while(isspace((unsigned char)*s))s++;
        if(!strncmp(s, ".equ ", 5)||!strncmp(s, ".equ\t", 5)) {
            s+=5;
            while(isspace((unsigned char)*s))s++;
            char *p=s;
            while(isalnum((unsigned char)*p)||*p=='_')p++;
            if(count<1024&&p>s) {
                size_t len=(size_t)(p-s);
                if(len>95)len=95;
                memcpy(names[count].name, s, len);
                names[count++].name[len]=0;
            }
        }
        else {
            char *p=s;
            while(isalnum((unsigned char)*p)||*p=='_')p++;
            if(*p==':'&&count<1024) {
                size_t len=(size_t)(p-s);
                if(len>95)len=95;
                memcpy(names[count].name, s, len);
                names[count++].name[len]=0;
            }
        }
        s=next;
    }
    free(copy);
    *out=NULL;
    size_t length=0;
    addtext(out, &length, monitor, strlen(monitor));
    addtext(out, &length, "\n", 1);
    copy=strdup(basic);
    int bye=0;
    for(char *line=copy;line;) {
        char *next=strchr(line, '\n');
        if(next)*next++=0;
        char *p=line;
        while(isspace((unsigned char)*p))p++;
        if(!strncmp(p, ".entry ", 7)||!strcmp(p, ".quad STACK, start")) {
            line=next;
            continue;
        }
        if(!strncmp(p, "do_bye:", 7))bye=1;
        else {
            char *q=p;
            while(isalnum((unsigned char)*q)||*q=='_')q++;
            if(*q==':')bye=0;
        }
        if(!strcmp(p, "HALT")) {
            const char *jump=bye?"    JMP monitor_prompt\n":"    JMP monitor_shutdown\n";
            addtext(out, &length, jump, strlen(jump));
            line=next;
            continue;
        }
        int quote=0, escape=0;
        for(p=line;*p;) {
            if(!quote&&*p==';') {
                addtext(out, &length, p, strlen(p));
                break;
            }
            if(quote) {
                char c=*p++;
                addtext(out, &length, &c, 1);
                if(escape)escape=0;
                else if(c=='\\')escape=1;
                else if(c==quote)quote=0;
                continue;
            }
            if(*p=='"'||*p=='\'') {
                quote=*p;
                addtext(out, &length, p++, 1);
                continue;
            }
            if(isalpha((unsigned char)*p)||*p=='_') {
                char *q=p;
                while(isalnum((unsigned char)*q)||*q=='_')q++;
                int found=0;
                for(int i=0;i<count;i++)if(strlen(names[i].name)==(size_t)(q-p)&&!strncmp(names[i].name, p, (size_t)(q-p))) {
                    found=1;
                    break;
                }
                if(found)addtext(out, &length, "basic_", 6);
                addtext(out, &length, p, (size_t)(q-p));
                p=q;
            }
            else addtext(out, &length, p++, 1);
        }
        addtext(out, &length, "\n", 1);
        line=next;
    }
    free(copy);
    addtext(out, &length, native, strlen(native));
    addtext(out, &length, "\n", 1);
    return 1;
}
int writefile(const char *path, const void *p, size_t n) {
    FILE *f=fopen(path, "wb");
    if(!f)return 0;
    int ok=!n||fwrite(p, 1, n, f)==n;
    int closeok=fclose(f)==0;
    return ok&&closeok;
}
