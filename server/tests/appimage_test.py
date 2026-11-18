# Run from the repository root after make -C server. Tests the actual native binding.
from pathlib import Path
s=Path('server/src/ui/js_sys.c').read_text(); f=s[s.index('static JSValue f_fs_appimage('):s.index('/* lord.fs.stat(path)')]
pre='''#define _GNU_SOURCE
#include "quickjs.h"
#include <sys/stat.h>
#include <spawn.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
static int resolve_in_home(const char *p,char *out){return realpath(p,out)?0:-1;}
static const char *lerr_errno(int e){return strerror(e);}
#define js_throw_code(c,...) JS_ThrowInternalError(c,"action failed")
'''
post='''
int main(int argc,char **argv){
JSRuntime *r=JS_NewRuntime();JSContext *c=JS_NewContext(r);
JSValue a[2]={JS_NewString(c,argv[1]),JS_TRUE}; struct stat st;
assert(!chmod(argv[1],0640));
assert(!JS_IsException(f_fs_appimage(c,JS_UNDEFINED,2,a)));
assert(!stat(argv[1],&st) && (st.st_mode&0777)==0740);
assert(!JS_IsException(f_fs_appimage(c,JS_UNDEFINED,1,a)));
a[1]=JS_FALSE;assert(!JS_IsException(f_fs_appimage(c,JS_UNDEFINED,2,a)));
assert(!stat(argv[1],&st) && (st.st_mode&0777)==0640);
assert(JS_IsException(f_fs_appimage(c,JS_UNDEFINED,1,a)));
JS_FreeValue(c,JS_GetException(c));JS_FreeValue(c,a[0]);
a[0]=JS_NewString(c,"/tmp/ordinary.txt"); assert(JS_IsException(f_fs_appimage(c,JS_UNDEFINED,2,a)));
JS_FreeValue(c,JS_GetException(c));JS_FreeValue(c,a[0]);JS_FreeContext(c);JS_FreeRuntime(r);puts("appimage permissions, spaced path, launch and refusal: OK");}
'''
import tempfile, subprocess, shutil
with tempfile.TemporaryDirectory(prefix='lordos-appimage-test-') as folder:
    base = Path(folder)
    source = base / 'test.c'
    source.write_text(pre + f + post)
    binary = base / 'test'
    objects = ['quickjs', 'libregexp', 'libunicode', 'cutils', 'dtoa']
    subprocess.run(['cc', '-Ithird_party/quickjs', str(source), *[f'server/build/qjs/{name}.o' for name in objects], '-lm', '-lpthread', '-ldl', '-o', str(binary)], check=True)
    image = base / 'test space.AppImage'
    shutil.copyfile('/bin/true', image)
    subprocess.run([str(binary), str(image)], check=True)

