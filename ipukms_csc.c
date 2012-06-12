#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <stdint.h>
#include <drm/imx-ipu-v3-ioctls.h>
#include <png.h>


#define WIDTH 640
#define HEIGHT 360

#include "ipukms_png.c"

struct csc_bo {
	uint32_t handle;
	uint32_t size;
	uint32_t phys;
	void *ptr;
	int map_count;
	uint32_t pitch;
};

struct csc_bo *csc_bo_create(int fd,
			const unsigned width, const unsigned height,
			const unsigned bpp)
{
	struct drm_mode_create_dumb arg;
	struct csc_bo *bo;
	int ret;

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return NULL;

	memset(&arg, 0, sizeof(arg));
	arg.width = width;
	arg.height = height;
	arg.bpp = bpp;
	
	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &arg);
	printf("%s: ret:%d\n", __func__, ret);
	if (ret < 0)
		goto err_free;

	bo->handle = arg.handle;
	bo->size = arg.size;
	bo->pitch = arg.pitch;
	bo->phys = ret;

	return bo;
 err_free:
	free(bo);
	return NULL;
}

static int csc_bo_map(int fd, struct csc_bo *bo)
{
	struct drm_mode_map_dumb arg;
	int ret;
	void *map;

	if (bo->ptr) {
		bo->map_count++;
		return 0;
	}

	memset(&arg, 0, sizeof(arg));
	arg.handle = bo->handle;

	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &arg);
	if (ret)
		return ret;

	map = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   fd, arg.offset);
	if (map == MAP_FAILED)
		return -errno;

	bo->ptr = map;
	return 0;
}

static int csc_bo_destroy(int fd, struct csc_bo *bo)
{
	struct drm_mode_destroy_dumb arg;
	int ret;
	
	if (bo->ptr) {
		munmap(bo->ptr, bo->size);
		bo->ptr = NULL;
	}

	memset(&arg, 0, sizeof(arg));
	arg.handle = bo->handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);
	if (ret)
		return -errno;

	free(bo);
	return 0;
}

int main(void)
{
	struct csc_bo* input_bo;
	struct csc_bo* output_bo;
	int ret, fd;

	if ((fd = drmOpen("imx-drm", NULL)) < 0) {
		printf("drmOpen failed.\n");
		return -1;
	}

	//create buffer for csc in/out
	input_bo = csc_bo_create(fd, WIDTH, HEIGHT, 32);
	if (!input_bo) {
		printf("Couldn't create input gem bo\n");
		return -1;
	}
	if (csc_bo_map(fd, input_bo)) {
		printf("Couldn't map input bo\n");
		return -1;
	}

	output_bo = csc_bo_create(fd, WIDTH, HEIGHT, 32);
	if (!output_bo) {
		printf("Couldn't create output gem bo\n");
		return -1;
	}

	struct drm_imx_ipu_queue req = {
		.task = IPU_TASK_CSC,
		.input = {
			.phys = input_bo->phys, //id of gem_cma object
			.pix = {
				.pixelformat = V4L2_PIX_FMT_UYVY,
				.bytesperline = 2 * WIDTH,
				.width = WIDTH,
				.height = HEIGHT,
			},
			.rect = {
				.width = WIDTH,
				.height = HEIGHT,
			},
		},
		.output = {
			.phys = output_bo->phys, //id of gem_cma object
			.pix = {
				.pixelformat = V4L2_PIX_FMT_RGB32,
				.bytesperline = 4 * WIDTH,
				//.pixelformat = V4L2_PIX_FMT_YUV420,
				//.bytesperline = 1 * WIDTH,
				.width = WIDTH,
				.height = HEIGHT,
			},
			.rect = {
				.width = WIDTH,
				.height = HEIGHT,
			},
		},
#if 0
		.csc_coeffs[4][3] = {
			{149, 0, 0},	/* C00, C01, C02 */
			{149, 0, 0},	/* C10, C11, C12 */
			{149, 0, 0},	/* C20, C21, C22 */
			{0, 0, 0},	/*  A0,  A1,  A2 */
		},
#endif
	};
	uint32_t *c0, *c1, *c2, *a0;
	c0 = calloc(3, sizeof(uint32_t));
	c1 = calloc(3, sizeof(uint32_t));
	c2 = calloc(3, sizeof(uint32_t));
	a0 = calloc(3, sizeof(uint32_t));

	printf("%p %p %p %p\n", c0, c1, c2, a0);

	c0[0] = 149;
	c1[0] = 149;
	c2[0] = 149;

	req.csc_coeffs[0] = c0;
	req.csc_coeffs[1] = c1;
	req.csc_coeffs[2] = c2;
	req.csc_coeffs[3] = a0;

	/* Open YUV420P sample file */
	FILE *fp = fopen("output.gray", "r");
	srand(time(NULL));
	//fseek(fp, WIDTH*HEIGHT*3/2*(rand()%87), 0);
	fread(input_bo->ptr, 1, WIDTH*HEIGHT*2, fp);
	fclose(fp);

	//convert to YUV420
	if (drmCommandWriteRead(fd, DRM_IMX_IPU_QUEUE, &req, sizeof(req))) {
		printf("drmCommandWriteRead failed. [%s]. (%d)\n", strerror(errno), sizeof(req));
		return -errno;
	}

	if (csc_bo_map(fd, output_bo)) {
		printf("Couldn't map output bo\n");
		return -1;
	}

	//let's see it!
	char* rgb_matrix = (char*) malloc(WIDTH * HEIGHT * 3);
	const char * rgb = (const char *)output_bo->ptr;

	//rgba565_to_rgb888(rgb, rgb_matrix, WIDTH*HEIGHT);
	rgba8888_to_rgb888(rgb, rgb_matrix, WIDTH*HEIGHT);

	save_png("output.png", rgb_matrix, WIDTH, HEIGHT);
	free(rgb_matrix);
	return 0;
}
