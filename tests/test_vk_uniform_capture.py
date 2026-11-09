"""Captured uniforms must preserve ordered partial updates and alignment."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def harness():
    core = (ROOT / 'nxnative/dm/android/gfx_vk_core.c').read_text()
    def function(name):
        start = core.index(name)
        return core[start:core.index('\n}\n', start) + 3]
    return HARNESS.replace('FUNCTIONS', function('int vk_gpu_pending(') + function('void vk_ubd_read(') + function('int vk_ubd_shadow('))


HARNESS = r"""
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef uint64_t VkDeviceSize;
typedef uint8_t *VkBuffer;
static struct { struct { struct { uint64_t minUniformBufferOffsetAlignment; } limits; } props; } V = {{{16}}};
static struct { uint64_t lo,hi,so; uint8_t *p; VkBuffer sb; } g_ubd[16];
static struct { uint64_t lo,hi,fence; uint8_t *data; } g_infl[16];
static int g_nubd,g_ninfl,g_x_noshc=1;
static uint64_t g_ubd_gen,g_ubd_racy,g_ubd_bytes,g_ubd_shadows;
static uint8_t scratch[1024];
static struct { uint64_t lo, hi, fence; } g_gw[16];
static int g_ngw;
static uint64_t vk_fence_done(void) {return 1;}
#define VKLOG(...) ((void)0)
static int vk_ubd_overlaps(uint64_t a,uint64_t n) {for(int i=0;i<g_nubd;i++)if(g_ubd[i].lo<a+n&&a<g_ubd[i].hi)return 1;return 0;}
static uint8_t *vk_scratch(uint64_t n,uint64_t al,VkBuffer *b,VkDeviceSize *o) {(void)n;(void)al;*b=scratch;*o=0;return scratch;}
FUNCTIONS
int main(void) {
 uint8_t memory[512],expected[512],capture[16][512],got[512];
 g_gw[0].lo=100;g_gw[0].hi=200;g_gw[0].fence=1;
 g_gw[1].lo=300;g_gw[1].hi=400;g_gw[1].fence=2;g_ngw=2;
 assert(!vk_gpu_pending(120,4));assert(vk_gpu_pending(320,4));
 assert(!vk_gpu_pending(250,4));g_ngw=1;assert(!vk_gpu_pending(120,4));g_ngw=0;
 srand(42);int reused=0,merged=0;
 for(int trial=0;trial<10000;trial++) {
  for(int j=0;j<512;j++)memory[j]=(uint8_t)rand();memcpy(expected,memory,512);
  g_ninfl=rand()%4;g_nubd=rand()%9;g_ubd_gen++;
  for(int i=0;i<g_ninfl+g_nubd;i++) {
   int off=rand()%384,n=1+rand()%128;for(int j=0;j<n;j++)capture[i][j]=(uint8_t)rand();
   uint64_t a=(uintptr_t)memory+off;
   if(i<g_ninfl) {g_infl[i].lo=a;g_infl[i].hi=a+n;g_infl[i].fence=(i%2)+1;g_infl[i].data=capture[i];if(g_infl[i].fence>1)memcpy(expected+off,capture[i],n);}
   else {int k=i-g_ninfl;g_ubd[k].lo=a;g_ubd[k].hi=a+n;g_ubd[k].p=capture[i];g_ubd[k].sb=capture[i];g_ubd[k].so=0;memcpy(expected+off,capture[i],n);}
  }
  for(int read=0;read<20;read++) {
   int off=rand()%384,n=1+rand()%128;uint64_t a=(uintptr_t)memory+off;
   vk_ubd_read(a,n,got);assert(!memcmp(got,expected+off,n));
   VkBuffer b;VkDeviceSize o;
   if(vk_ubd_shadow(a,n,&b,&o)) {assert(!memcmp(b+o,expected+off,n));if(b==scratch)merged++;else reused++;}
  }
 }
 printf("200000 reads passed; %d captures reused, %d partial overlaps merged\n",reused,merged);
}
"""


class UniformCapture(unittest.TestCase):
    @unittest.skipUnless(shutil.which('cc'), 'a host C compiler is required')
    def test_ordered_overlaps(self):
        with tempfile.TemporaryDirectory(prefix='nx2a-uniform-') as tmp:
            source, binary = Path(tmp) / 'check.c', Path(tmp) / 'check'
            source.write_text(harness())
            subprocess.run(['cc', '-std=c11', '-O2', str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    unittest.main()
