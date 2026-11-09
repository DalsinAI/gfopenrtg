/* A virglrenderer that loads and whose init fails. test_env_restore.sh
 * dlopens it in place of the real one, so the software try can be checked
 * on a machine with no GPU. Copyright (c) 2026 AmiCradle Team. MIT. */
#include <stdio.h>
#include <stdlib.h>

int virgl_renderer_init(void *cookie, int flags, void *cb)
{
    const char *mesa = getenv("MESA_LOADER_DRIVER_OVERRIDE");
    const char *gallium = getenv("GALLIUM_DRIVER");
    const char *soft = getenv("LIBGL_ALWAYS_SOFTWARE");
    const char *vk = getenv("VK_DRIVER_FILES");
    FILE *f;
    (void)cookie; (void)flags; (void)cb;
    f = fopen("/tmp/av-env-seen.txt", "a");
    if (f) {
        fprintf(f, "mesa=%s gallium=%s soft=%s vk=%s\n",
                mesa ? mesa : "-", gallium ? gallium : "-", soft ? soft : "-", vk ? vk : "-");
        fclose(f);
    }
    return 1;
}
int virgl_renderer_cleanup(void *cookie) { (void)cookie; return 0; }
void virgl_renderer_get_cap_set(unsigned int set, unsigned int *max_ver, unsigned int *max_size)
{ (void)set; if (max_ver) *max_ver = 0; if (max_size) *max_size = 0; }
void virgl_renderer_fill_caps(unsigned int set, unsigned int version, void *caps)
{ (void)set; (void)version; (void)caps; }
int virgl_renderer_context_create(unsigned int handle, unsigned int nlen, const char *name)
{ (void)handle; (void)nlen; (void)name; return 0; }
void virgl_renderer_context_destroy(unsigned int handle) { (void)handle; }
int virgl_renderer_resource_create(void *args, void *iov, unsigned int niov)
{ (void)args; (void)iov; (void)niov; return 0; }
void virgl_renderer_resource_unref(unsigned int res_handle) { (void)res_handle; }
void virgl_renderer_ctx_attach_resource(int ctx_id, int res_handle)
{ (void)ctx_id; (void)res_handle; }
void virgl_renderer_ctx_detach_resource(int ctx_id, int res_handle)
{ (void)ctx_id; (void)res_handle; }
int virgl_renderer_submit_cmd(void *buffer, int ctx_id, int ndw)
{ (void)buffer; (void)ctx_id; (void)ndw; return 0; }
int virgl_renderer_transfer_read_iov(void *cookie, unsigned int handle, void *ctx, int level, unsigned int stride, unsigned int layer_stride, void *box, unsigned long offset, void *iov, int iovec_cnt)
{ (void)cookie; (void)handle; (void)ctx; (void)level; (void)stride; (void)layer_stride; (void)box; (void)offset; (void)iov; (void)iovec_cnt; return 0; }
int virgl_renderer_transfer_write_iov(unsigned int handle, void *ctx, int level, unsigned int stride, unsigned int layer_stride, void *box, unsigned long offset, void *iov, unsigned int iovec_cnt)
{ (void)handle; (void)ctx; (void)level; (void)stride; (void)layer_stride; (void)box; (void)offset; (void)iov; (void)iovec_cnt; return 0; }
