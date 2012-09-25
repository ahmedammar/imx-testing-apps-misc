#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <stdint.h>
#include <drm/imx-ipu-v3-ioctls.h>
#include <png.h>

// default width/height of input files
#define WIDTH 640
#define HEIGHT 360

// png creation helper functions
#include "ipukms_png.c"

// structure to hold DRM framebuffer information
struct csc_bo {
	uint32_t handle;
	uint32_t size;
	uint32_t phys;
	void *ptr;
	int map_count;
	uint32_t pitch;
};

// framebuffer creation function
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

// framebuffer userspace mapping function
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

// framebuffer destruction function
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

// main function
int main(void)
{
	struct csc_bo* input_bo;
	struct csc_bo* output_bo;
	int ret, fd;

	// open DRM device
	if ((fd = drmOpen("imx-drm", "platform:imx-drm:00")) < 0) {
		printf("drmOpen failed.\n");
		return -1;
	}

	// create buffer for csc input
	input_bo = csc_bo_create(fd, WIDTH, HEIGHT, 32);
	if (!input_bo) {
		printf("Couldn't create input gem bo\n");
		return -1;
	}

	// map the input buffer
	if (csc_bo_map(fd, input_bo)) {
		printf("Couldn't map input bo\n");
		return -1;
	}

	// create buffer for csc output
	output_bo = csc_bo_create(fd, WIDTH, HEIGHT, 32);
	if (!output_bo) {
		printf("Couldn't create output gem bo\n");
		return -1;
	}

	// create ipu csc request
	struct drm_imx_ipu_queue req = {
		.task = IPU_TASK_CSC,
		.input = {
			.phys = input_bo->phys,
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
			.phys = output_bo->phys,
			.pix = {
				.pixelformat = V4L2_PIX_FMT_RGB32,
				.bytesperline = 4 * WIDTH,
				.width = WIDTH,
				.height = HEIGHT,
			},
			.rect = {
				.width = WIDTH,
				.height = HEIGHT,
			},
		},
	};

	// color-space conversion (csc) coefficient matrix values
	uint32_t *c0, *c1, *c2, *a0;

	/* i.MX6 Reference Manual (Chapter 36: Table 36-18)
	 * Z0 = X0*C00 + X1*C01 +X2*C02+A0;
	 * Z1 = X0*C10 + X1*C11 +X2*C12+A1;
	 * Z2 = X0*C20 + X1*C21 +X2*C22+A2;
	 *
	 * Where c0[0] == C00, c0[1] == C01, c0[2] == C02
	 */

	// allocate csc coefficients for kernel task
	c0 = calloc(3, sizeof(uint32_t)); 
	c1 = calloc(3, sizeof(uint32_t));
	c2 = calloc(3, sizeof(uint32_t));
	a0 = calloc(3, sizeof(uint32_t));

	// converting grayscale to RGB32 R = G = B = Y (dropping chroma)
	// Z0 = R = X0 * C00 
	// Z1 = G = X0 * C10
	// Z2 = B = X0 * C20 
	//
	// C00, C10, C20 = 0.99
	c0[0] = 255;
	c1[0] = 255;
	c2[0] = 255;

	// set coefficients for kernel ioctl request
	req.csc_coeffs[0] = c0;
	req.csc_coeffs[1] = c1;
	req.csc_coeffs[2] = c2;
	req.csc_coeffs[3] = a0;

	// open grayscale sample input file
	FILE *fp = fopen("output.gray", "r");

	// another version using a larger sample
	// FILE *fp = fopen("BigBuckBunny_640x360_small.yuv", "r");
	// srand(time(NULL));
	// fseek(fp, WIDTH*HEIGHT*3/2*(rand()%87), 0);

	// read input into input framebuffer
	fread(input_bo->ptr, 1, WIDTH*HEIGHT*2, fp);

	// close read file
	fclose(fp);

	// convert to input framebuffer RGB32
	if (drmCommandWriteRead(fd, DRM_IMX_IPU_QUEUE, &req, sizeof(req))) {
		printf("drmCommandWriteRead failed. [%s]. (%d)\n", strerror(errno), sizeof(req));
		return -errno;
	}

	// map the output framebuffer to userspace
	if (csc_bo_map(fd, output_bo)) {
		printf("Couldn't map output bo\n");
		return -1;
	}

	// let's see it!
	// save output framebuffer to png (png only supports rgb24)

	// allocate the rgb24 output array
	char* rgb_matrix = (char*) malloc(WIDTH * HEIGHT * 3);
	const char * rgb = (const char *)output_bo->ptr;

	// convert rgb32 to rgb24 using helper function
	rgba8888_to_rgb888(rgb, rgb_matrix, WIDTH*HEIGHT);

	// save output png using helper function
	save_png("output.png", rgb_matrix, WIDTH, HEIGHT);

	// free the allocated rgb24 matrix
	free(rgb_matrix);

	return 0;
}
