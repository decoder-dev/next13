/*
 * v4l2-dv-timings - dv-timings helper functions
 *
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/rational.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-dv-timings.h>
#include <linux/math64.h>

MODULE_AUTHOR("Hans Verkuil");
MODULE_DESCRIPTION("V4L2 DV Timings Helper Functions");
MODULE_LICENSE("GPL");

const struct v4l2_dv_timings v4l2_dv_timings_presets[] = {
	V4L2_DV_BT_CEA_640X480P59_94,
	V4L2_DV_BT_CEA_720X480I59_94,
	V4L2_DV_BT_CEA_720X480P59_94,
	V4L2_DV_BT_CEA_720X576I50,
	V4L2_DV_BT_CEA_720X576P50,
	V4L2_DV_BT_CEA_1280X720P24,
	V4L2_DV_BT_CEA_1280X720P25,
	V4L2_DV_BT_CEA_1280X720P30,
	V4L2_DV_BT_CEA_1280X720P50,
	V4L2_DV_BT_CEA_1280X720P60,
	V4L2_DV_BT_CEA_1920X1080P24,
	V4L2_DV_BT_CEA_1920X1080P25,
	V4L2_DV_BT_CEA_1920X1080P30,
	V4L2_DV_BT_CEA_1920X1080I50,
	V4L2_DV_BT_CEA_1920X1080P50,
	V4L2_DV_BT_CEA_1920X1080I60,
	V4L2_DV_BT_CEA_1920X1080P60,
	V4L2_DV_BT_DMT_640X350P85,
	V4L2_DV_BT_DMT_640X400P85,
	V4L2_DV_BT_DMT_720X400P85,
	V4L2_DV_BT_DMT_640X480P72,
	V4L2_DV_BT_DMT_640X480P75,
	V4L2_DV_BT_DMT_640X480P85,
	V4L2_DV_BT_DMT_800X600P56,
	V4L2_DV_BT_DMT_800X600P60,
	V4L2_DV_BT_DMT_800X600P72,
	V4L2_DV_BT_DMT_800X600P75,
	V4L2_DV_BT_DMT_800X600P85,
	V4L2_DV_BT_DMT_800X600P120_RB,
	V4L2_DV_BT_DMT_848X480P60,
	V4L2_DV_BT_DMT_1024X768I43,
	V4L2_DV_BT_DMT_1024X768P60,
	V4L2_DV_BT_DMT_1024X768P70,
	V4L2_DV_BT_DMT_1024X768P75,
	V4L2_DV_BT_DMT_1024X768P85,
	V4L2_DV_BT_DMT_1024X768P120_RB,
	V4L2_DV_BT_DMT_1152X864P75,
	V4L2_DV_BT_DMT_1280X768P60_RB,
	V4L2_DV_BT_DMT_1280X768P60,
	V4L2_DV_BT_DMT_1280X768P75,
	V4L2_DV_BT_DMT_1280X768P85,
	V4L2_DV_BT_DMT_1280X768P120_RB,
	V4L2_DV_BT_DMT_1280X800P60_RB,
	V4L2_DV_BT_DMT_1280X800P60,
	V4L2_DV_BT_DMT_1280X800P75,
	V4L2_DV_BT_DMT_1280X800P85,
	V4L2_DV_BT_DMT_1280X800P120_RB,
	V4L2_DV_BT_DMT_1280X960P60,
	V4L2_DV_BT_DMT_1280X960P85,
	V4L2_DV_BT_DMT_1280X960P120_RB,
	V4L2_DV_BT_DMT_1280X1024P60,
	V4L2_DV_BT_DMT_1280X1024P75,
	V4L2_DV_BT_DMT_1280X1024P85,
	V4L2_DV_BT_DMT_1280X1024P120_RB,
	V4L2_DV_BT_DMT_1360X768P60,
	V4L2_DV_BT_DMT_1360X768P120_RB,
	V4L2_DV_BT_DMT_1366X768P60,
	V4L2_DV_BT_DMT_1366X768P60_RB,
	V4L2_DV_BT_DMT_1400X1050P60_RB,
	V4L2_DV_BT_DMT_1400X1050P60,
	V4L2_DV_BT_DMT_1400X1050P75,
	V4L2_DV_BT_DMT_1400X1050P85,
	V4L2_DV_BT_DMT_1400X1050P120_RB,
	V4L2_DV_BT_DMT_1440X900P60_RB,
	V4L2_DV_BT_DMT_1440X900P60,
	V4L2_DV_BT_DMT_1440X900P75,
	V4L2_DV_BT_DMT_1440X900P85,
	V4L2_DV_BT_DMT_1440X900P120_RB,
	V4L2_DV_BT_DMT_1600X900P60_RB,
	V4L2_DV_BT_DMT_1600X1200P60,
	V4L2_DV_BT_DMT_1600X1200P65,
	V4L2_DV_BT_DMT_1600X1200P70,
	V4L2_DV_BT_DMT_1600X1200P75,
	V4L2_DV_BT_DMT_1600X1200P85,
	V4L2_DV_BT_DMT_1600X1200P120_RB,
	V4L2_DV_BT_DMT_1680X1050P60_RB,
	V4L2_DV_BT_DMT_1680X1050P60,
	V4L2_DV_BT_DMT_1680X1050P75,
	V4L2_DV_BT_DMT_1680X1050P85,
	V4L2_DV_BT_DMT_1680X1050P120_RB,
	V4L2_DV_BT_DMT_1792X1344P60,
	V4L2_DV_BT_DMT_1792X1344P75,
	V4L2_DV_BT_DMT_1792X1344P120_RB,
	V4L2_DV_BT_DMT_1856X1392P60,
	V4L2_DV_BT_DMT_1856X1392P75,
	V4L2_DV_BT_DMT_1856X1392P120_RB,
	V4L2_DV_BT_DMT_1920X1200P60_RB,
	V4L2_DV_BT_DMT_1920X1200P60,
	V4L2_DV_BT_DMT_1920X1200P75,
	V4L2_DV_BT_DMT_1920X1200P85,
	V4L2_DV_BT_DMT_1920X1200P120_RB,
	V4L2_DV_BT_DMT_1920X1440P60,
	V4L2_DV_BT_DMT_1920X1440P75,
	V4L2_DV_BT_DMT_1920X1440P120_RB,
	V4L2_DV_BT_DMT_2048X1152P60_RB,
	V4L2_DV_BT_DMT_2560X1600P60_RB,
	V4L2_DV_BT_DMT_2560X1600P60,
	V4L2_DV_BT_DMT_2560X1600P75,
	V4L2_DV_BT_DMT_2560X1600P85,
	V4L2_DV_BT_DMT_2560X1600P120_RB,
	V4L2_DV_BT_CEA_3840X2160P24,
	V4L2_DV_BT_CEA_3840X2160P25,
	V4L2_DV_BT_CEA_3840X2160P30,
	V4L2_DV_BT_CEA_3840X2160P50,
	V4L2_DV_BT_CEA_3840X2160P60,
	V4L2_DV_BT_CEA_4096X2160P24,
	V4L2_DV_BT_CEA_4096X2160P25,
	V4L2_DV_BT_CEA_4096X2160P30,
	V4L2_DV_BT_CEA_4096X2160P50,
	V4L2_DV_BT_DMT_4096X2160P59_94_RB,
	V4L2_DV_BT_CEA_4096X2160P60,
	{ }
};
EXPORT_SYMBOL_GPL(v4l2_dv_timings_presets);

bool v4l2_valid_dv_timings(const struct v4l2_dv_timings *t,
			   const struct v4l2_dv_timings_cap *dvcap,
			   v4l2_check_dv_timings_fnc fnc,
			   void *fnc_handle)
{
	const struct v4l2_bt_timings *bt = &t->bt;
	const struct v4l2_bt_timings_cap *cap = &dvcap->bt;
	u32 caps = cap->capabilities;
	const u32 max_vert = 10240;
	u32 max_hor = 3 * bt->width;

	if (t->type != V4L2_DV_BT_656_1120)
		return false;
	if (t->type != dvcap->type ||
	    bt->height < cap->min_height ||
	    bt->height > cap->max_height ||
	    bt->width < cap->min_width ||
	    bt->width > cap->max_width ||
	    bt->pixelclock < cap->min_pixelclock ||
	    bt->pixelclock > cap->max_pixelclock ||
	    (!(caps & V4L2_DV_BT_CAP_CUSTOM) &&
	     cap->standards && bt->standards &&
	     !(bt->standards & cap->standards)) ||
	    (bt->interlaced && !(caps & V4L2_DV_BT_CAP_INTERLACED)) ||
	    (!bt->interlaced && !(caps & V4L2_DV_BT_CAP_PROGRESSIVE)))
		return false;

	/* sanity checks for the blanking timings */
	if (!bt->interlaced &&
	    (bt->il_vbackporch || bt->il_vsync || bt->il_vfrontporch))
		return false;
	/*
	 * Some video receivers cannot properly separate the frontporch,
	 * backporch and sync values, and instead they only have the total
	 * blanking. That can be assigned to any of these three fields.
	 * So just check that none of these are way out of range.
	 */
	if (bt->hfrontporch > max_hor ||
	    bt->hsync > max_hor || bt->hbackporch > max_hor)
		return false;
	if (bt->vfrontporch > max_vert ||
	    bt->vsync > max_vert || bt->vbackporch > max_vert)
		return false;
	if (bt->interlaced && (bt->il_vfrontporch > max_vert ||
	    bt->il_vsync > max_vert || bt->il_vbackporch > max_vert))
		return false;
	return fnc == NULL || fnc(t, fnc_handle);
}
EXPORT_SYMBOL_GPL(v4l2_valid_dv_timings);

int v4l2_enum_dv_timings_cap(struct v4l2_enum_dv_timings *t,
			     const struct v4l2_dv_timings_cap *cap,
			     v4l2_check_dv_timings_fnc fnc,
			     void *fnc_handle)
{
	u32 i, idx;

	memset(t->reserved, 0, sizeof(t->reserved));
	for (i = idx = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
		if (v4l2_valid_dv_timings(v4l2_dv_timings_presets + i, cap,
					  fnc, fnc_handle) &&
		    idx++ == t->index) {
			t->timings = v4l2_dv_timings_presets[i];
			return 0;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(v4l2_enum_dv_timings_cap);

bool v4l2_find_dv_timings_cap(struct v4l2_dv_timings *t,
			      const struct v4l2_dv_timings_cap *cap,
			      unsigned pclock_delta,
			      v4l2_check_dv_timings_fnc fnc,
			      void *fnc_handle)
{
	int i;

	if (!v4l2_valid_dv_timings(t, cap, fnc, fnc_handle))
		return false;

	for (i = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
		if (v4l2_valid_dv_timings(v4l2_dv_timings_presets + i, cap,
					  fnc, fnc_handle) &&
		    v4l2_match_dv_timings(t, v4l2_dv_timings_presets + i,
					  pclock_delta, false)) {
			u32 flags = t->bt.flags & V4L2_DV_FL_REDUCED_FPS;

			*t = v4l2_dv_timings_presets[i];
			if (can_reduce_fps(&t->bt))
				t->bt.flags |= flags;

			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL_GPL(v4l2_find_dv_timings_cap);

bool v4l2_find_dv_timings_cea861_vic(struct v4l2_dv_timings *t, u8 vic)
{
	unsigned int i;

	for (i = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
		const struct v4l2_bt_timings *bt =
			&v4l2_dv_timings_presets[i].bt;

		if ((bt->flags & V4L2_DV_FL_HAS_CEA861_VIC) &&
		    bt->cea861_vic == vic) {
			*t = v4l2_dv_timings_presets[i];
			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL_GPL(v4l2_find_dv_timings_cea861_vic);

/**
 * v4l2_match_dv_timings - check if two timings match
 * @t1 - compare this v4l2_dv_timings struct...
 * @t2 - with this struct.
 * @pclock_delta - the allowed pixelclock deviation.
 * @match_reduced_fps - if true, then fail if V4L2_DV_FL_REDUCED_FPS does not
 * match.
 *
 * Compare t1 with t2 with a given margin of error for the pixelclock.
 */
bool v4l2_match_dv_timings(const struct v4l2_dv_timings *t1,
			   const struct v4l2_dv_timings *t2,
			   unsigned pclock_delta, bool match_reduced_fps)
{
	if (t1->type != t2->type || t1->type != V4L2_DV_BT_656_1120)
		return false;
	if (t1->bt.width == t2->bt.width &&
	    t1->bt.height == t2->bt.height &&
	    t1->bt.interlaced == t2->bt.interlaced &&
	    t1->bt.polarities == t2->bt.polarities &&
	    t1->bt.pixelclock >= t2->bt.pixelclock - pclock_delta &&
	    t1->bt.pixelclock <= t2->bt.pixelclock + pclock_delta &&
	    t1->bt.hfrontporch == t2->bt.hfrontporch &&
	    t1->bt.hsync == t2->bt.hsync &&
	    t1->bt.hbackporch == t2->bt.hbackporch &&
	    t1->bt.vfrontporch == t2->bt.vfrontporch &&
	    t1->bt.vsync == t2->bt.vsync &&
	    t1->bt.vbackporch == t2->bt.vbackporch &&
	    (!match_reduced_fps ||
	     (t1->bt.flags & V4L2_DV_FL_REDUCED_FPS) ==
		(t2->bt.flags & V4L2_DV_FL_REDUCED_FPS)) &&
	    (!t1->bt.interlaced ||
		(t1->bt.il_vfrontporch == t2->bt.il_vfrontporch &&
		 t1->bt.il_vsync == t2->bt.il_vsync &&
		 t1->bt.il_vbackporch == t2->bt.il_vbackporch)))
		return true;
	return false;
}
EXPORT_SYMBOL_GPL(v4l2_match_dv_timings);

void v4l2_print_dv_timings(const char *dev_prefix, const char *prefix,
			   const struct v4l2_dv_timings *t, bool detailed)
{
	const struct v4l2_bt_timings *bt = &t->bt;
	u32 htot, vtot;
	u32 fps;

	if (t->type != V4L2_DV_BT_656_1120)
		return;

	htot = V4L2_DV_BT_FRAME_WIDTH(bt);
	vtot = V4L2_DV_BT_FRAME_HEIGHT(bt);
	if (bt->interlaced)
		vtot /= 2;

	fps = (htot * vtot) > 0 ? div_u64((100 * (u64)bt->pixelclock),
				  (htot * vtot)) : 0;

	if (prefix == NULL)
		prefix = "";

	pr_debug("%s: %s%ux%u%s%u.%u (%ux%u)\n", dev_prefix, prefix,
		bt->width, bt->height, bt->interlaced ? "i" : "p",
		fps / 100, fps % 100, htot, vtot);

	if (!detailed)
		return;

	pr_debug("%s: horizontal: fp = %u, %ssync = %u, bp = %u\n",
			dev_prefix, bt->hfrontporch,
			(bt->polarities & V4L2_DV_HSYNC_POS_POL) ? "+" : "-",
			bt->hsync, bt->hbackporch);
	pr_debug("%s: vertical: fp = %u, %ssync = %u, bp = %u\n",
			dev_prefix, bt->vfrontporch,
			(bt->polarities & V4L2_DV_VSYNC_POS_POL) ? "+" : "-",
			bt->vsync, bt->vbackporch);
	if (bt->interlaced)
		pr_debug("%s: vertical bottom field: fp = %u, %ssync = %u, bp = %u\n",
			dev_prefix, bt->il_vfrontporch,
			(bt->polarities & V4L2_DV_VSYNC_POS_POL) ? "+" : "-",
			bt->il_vsync, bt->il_vbackporch);
	pr_debug("%s: pixelclock: %llu\n", dev_prefix, bt->pixelclock);
	pr_debug("%s: flags (0x%x):%s%s%s%s%s%s%s%s%s%s\n",
			dev_prefix, bt->flags,
			(bt->flags & V4L2_DV_FL_REDUCED_BLANKING) ?
			" REDUCED_BLANKING" : "",
			((bt->flags & V4L2_DV_FL_REDUCED_BLANKING) &&
			 bt->vsync == 8) ? " (V2)" : "",
			(bt->flags & V4L2_DV_FL_CAN_REDUCE_FPS) ?
			" CAN_REDUCE_FPS" : "",
			(bt->flags & V4L2_DV_FL_REDUCED_FPS) ?
			" REDUCED_FPS" : "",
			(bt->flags & V4L2_DV_FL_HALF_LINE) ?
			" HALF_LINE" : "",
			(bt->flags & V4L2_DV_FL_IS_CE_VIDEO) ?
			" CE_VIDEO" : "",
			(bt->flags & V4L2_DV_FL_FIRST_FIELD_EXTRA_LINE) ?
			" FIRST_FIELD_EXTRA_LINE" : "",
			(bt->flags & V4L2_DV_FL_HAS_PICTURE_ASPECT) ?
			" HAS_PICTURE_ASPECT" : "",
			(bt->flags & V4L2_DV_FL_HAS_CEA861_VIC) ?
			" HAS_CEA861_VIC" : "",
			(bt->flags & V4L2_DV_FL_HAS_HDMI_VIC) ?
			" HAS_HDMI_VIC" : "");
	pr_debug("%s: standards (0x%x):%s%s%s%s%s\n", dev_prefix, bt->standards,
			(bt->standards & V4L2_DV_BT_STD_CEA861) ?  " CEA" : "",
			(bt->standards & V4L2_DV_BT_STD_DMT) ?  " DMT" : "",
			(bt->standards & V4L2_DV_BT_STD_CVT) ?  " CVT" : "",
			(bt->standards & V4L2_DV_BT_STD_GTF) ?  " GTF" : "",
			(bt->standards & V4L2_DV_BT_STD_SDI) ?  " SDI" : "");
	if (bt->flags & V4L2_DV_FL_HAS_PICTURE_ASPECT)
		pr_debug("%s: picture aspect (hor:vert): %u:%u\n", dev_prefix,
			bt->picture_aspect.numerator,
			bt->picture_aspect.denominator);
	if (bt->flags & V4L2_DV_FL_HAS_CEA861_VIC)
		pr_debug("%s: CEA-861 VIC: %u\n", dev_prefix, bt->cea861_vic);
	if (bt->flags & V4L2_DV_FL_HAS_HDMI_VIC)
		pr_debug("%s: HDMI VIC: %u\n", dev_prefix, bt->hdmi_vic);
}
EXPORT_SYMBOL_GPL(v4l2_print_dv_timings);

struct v4l2_fract v4l2_dv_timings_aspect_ratio(const struct v4l2_dv_timings *t)
{
	struct v4l2_fract ratio = { 1, 1 };
	unsigned long n, d;

	if (t->type != V4L2_DV_BT_656_1120)
		return ratio;
	if (!(t->bt.flags & V4L2_DV_FL_HAS_PICTURE_ASPECT))
		return ratio;

	ratio.numerator = t->bt.width * t->bt.picture_aspect.denominator;
	ratio.denominator = t->bt.height * t->bt.picture_aspect.numerator;

	rational_best_approximation(ratio.numerator, ratio.denominator,
				    ratio.numerator, ratio.denominator, &n, &d);
	ratio.numerator = n;
	ratio.denominator = d;
	return ratio;
}
EXPORT_SYMBOL_GPL(v4l2_dv_timings_aspect_ratio);

/*
 * CVT defines
 * Based on Coordinated Video Timings Standard
 * version 1.1 September 10, 2003
 */

#define CVT_PXL_CLK_GRAN	250000	/* pixel clock granularity */
#define CVT_PXL_CLK_GRAN_RB_V2 1000	/* granularity for reduced blanking v2*/

/* Normal blanking */
#define CVT_MIN_V_BPORCH	7	/* lines */
#define CVT_MIN_V_PORCH_RND	3	/* lines */
#define CVT_MIN_VSYNC_BP	550	/* min time of vsync + back porch (us) */
#define CVT_HSYNC_PERCENT       8       /* nominal hsync as percentage of line */

/* Normal blanking for CVT uses GTF to calculate horizontal blanking */
#define CVT_CELL_GRAN		8	/* character cell granularity */
#define CVT_M			600	/* blanking formula gradient */
#define CVT_C			40	/* blanking formula offset */
#define CVT_K			128	/* blanking formula scaling factor */
#define CVT_J			20	/* blanking formula scaling factor */
#define CVT_C_PRIME (((CVT_C - CVT_J) * CVT_K / 256) + CVT_J)
#define CVT_M_PRIME (CVT_K * CVT_M / 256)

/* Reduced Blanking */
#define CVT_RB_MIN_V_BPORCH    7       /* lines  */
#define CVT_RB_V_FPORCH        3       /* lines  */
#define CVT_RB_MIN_V_BLANK   460       /* us     */
#define CVT_RB_H_SYNC         32       /* pixels */
#define CVT_RB_H_BLANK       160       /* pixels */
/* Reduce blanking Version 2 */
#define CVT_RB_V2_H_BLANK     80       /* pixels */
#define CVT_RB_MIN_V_FPORCH    3       /* lines  */
#define CVT_RB_V2_MIN_V_FPORCH 1       /* lines  */
#define CVT_RB_V_BPORCH        6       /* lines  */

/** v4l2_detect_cvt - detect if the given timings follow the CVT standard
 * @frame_height - the total height of the frame (including blanking) in lines.
 * @hfreq - the horizontal frequency in Hz.
 * @vsync - the height of the vertical sync in lines.
 * @active_width - active width of image (does not include blanking). This
 * information is needed only in case of version 2 of reduced blanking.
 * In other cases, this parameter does not have any effect on timings.
 * @polarities - the horizontal and vertical polarities (same as struct
 *		v4l2_bt_timings polarities).
 * @interlaced - if this flag is true, it indicates interlaced format
 * @fmt - the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid CVT format. If so, then it will return true, and fmt will be filled
 * in with the found CVT timings.
 */
bool v4l2_detect_cvt(unsigned frame_height,
		     unsigned hfreq,
		     unsigned vsync,
		     unsigned active_width,
		     u32 polarities,
		     bool interlaced,
		     struct v4l2_dv_timings *fmt)
{
	int  v_fp, v_bp, h_fp, h_bp, hsync;
	int  frame_width, image_height, image_width;
	bool reduced_blanking;
	bool rb_v2 = false;
	unsigned pix_clk;

	if (vsync < 4 || vsync > 8)
		return false;

	if (polarities == V4L2_DV_VSYNC_POS_POL)
		reduced_blanking = false;
	else if (polarities == V4L2_DV_HSYNC_POS_POL)
		reduced_blanking = true;
	else
		return false;

	if (reduced_blanking && vsync == 8)
		rb_v2 = true;

	if (rb_v2 && active_width == 0)
		return false;

	if (!rb_v2 && vsync > 7)
		return false;

	if (hfreq == 0)
		return false;

	/* Vertical */
	if (reduced_blanking) {
		if (rb_v2) {
			v_bp = CVT_RB_V_BPORCH;
			v_fp = (CVT_RB_MIN_V_BLANK * hfreq) / 1000000 + 1;
			v_fp -= vsync + v_bp;

			if (v_fp < CVT_RB_V2_MIN_V_FPORCH)
				v_fp = CVT_RB_V2_MIN_V_FPORCH;
		} else {
			v_fp = CVT_RB_V_FPORCH;
			v_bp = (CVT_RB_MIN_V_BLANK * hfreq) / 1000000 + 1;
			v_bp -= vsync + v_fp;

			if (v_bp < CVT_RB_MIN_V_BPORCH)
				v_bp = CVT_RB_MIN_V_BPORCH;
		}
	} else {
		v_fp = CVT_MIN_V_PORCH_RND;
		v_bp = (CVT_MIN_VSYNC_BP * hfreq) / 1000000 + 1 - vsync;

		if (v_bp < CVT_MIN_V_BPORCH)
			v_bp = CVT_MIN_V_BPORCH;
	}

	if (interlaced)
		image_height = (frame_height - 2 * v_fp - 2 * vsync - 2 * v_bp) & ~0x1;
	else
		image_height = (frame_height - v_fp - vsync - v_bp + 1) & ~0x1;

	if (image_height < 0)
		return false;

	/* Aspect ratio based on vsync */
	switch (vsync) {
	case 4:
		image_width = (image_height * 4) / 3;
		break;
	case 5:
		image_width = (image_height * 16) / 9;
		break;
	case 6:
		image_width = (image_height * 16) / 10;
		break;
	case 7:
		/* special case */
		if (image_height == 1024)
			image_width = (image_height * 5) / 4;
		else if (image_height == 768)
			image_width = (image_height * 15) / 9;
		else
			return false;
		break;
	case 8:
		image_width = active_width;
		break;
	default:
		return false;
	}

	if (!rb_v2)
		image_width = image_width & ~7;

	/* Horizontal */
	if (reduced_blanking) {
		int h_blank;
		int clk_gran;

		h_blank = rb_v2 ? CVT_RB_V2_H_BLANK : CVT_RB_H_BLANK;
		clk_gran = rb_v2 ? CVT_PXL_CLK_GRAN_RB_V2 : CVT_PXL_CLK_GRAN;

		pix_clk = (image_width + h_blank) * hfreq;
		pix_clk = (pix_clk / clk_gran) * clk_gran;

		h_bp  = h_blank / 2;
		hsync = CVT_RB_H_SYNC;
		h_fp  = h_blank - h_bp - hsync;

		frame_width = image_width + h_blank;
	} else {
		unsigned ideal_duty_cycle_per_myriad =
			100 * CVT_C_PRIME - (CVT_M_PRIME * 100000) / hfreq;
		int h_blank;

		if (ideal_duty_cycle_per_myriad < 2000)
			ideal_duty_cycle_per_myriad = 2000;

		h_blank = image_width * ideal_duty_cycle_per_myriad /
					(10000 - ideal_duty_cycle_per_myriad);
		h_blank = (h_blank / (2 * CVT_CELL_GRAN)) * 2 * CVT_CELL_GRAN;

		pix_clk = (image_width + h_blank) * hfreq;
		pix_clk = (pix_clk / CVT_PXL_CLK_GRAN) * CVT_PXL_CLK_GRAN;

		h_bp = h_blank / 2;
		frame_width = image_width + h_blank;

		hsync = frame_width * CVT_HSYNC_PERCENT / 100;
		hsync = (hsync / CVT_CELL_GRAN) * CVT_CELL_GRAN;
		h_fp = h_blank - hsync - h_bp;
	}

	fmt->type = V4L2_DV_BT_656_1120;
	fmt->bt.polarities = polarities;
	fmt->bt.width = image_width;
	fmt->bt.height = image_height;
	fmt->bt.hfrontporch = h_fp;
	fmt->bt.vfrontporch = v_fp;
	fmt->bt.hsync = hsync;
	fmt->bt.vsync = vsync;
	fmt->bt.hbackporch = frame_width - image_width - h_fp - hsync;

	if (!interlaced) {
		fmt->bt.vbackporch = frame_height - image_height - v_fp - vsync;
		fmt->bt.interlaced = V4L2_DV_PROGRESSIVE;
	} else {
		fmt->bt.vbackporch = (frame_height - image_height - 2 * v_fp -
				      2 * vsync) / 2;
		fmt->bt.il_vbackporch = frame_height - image_height - 2 * v_fp -
					2 * vsync - fmt->bt.vbackporch;
		fmt->bt.il_vfrontporch = v_fp;
		fmt->bt.il_vsync = vsync;
		fmt->bt.flags |= V4L2_DV_FL_HALF_LINE;
		fmt->bt.interlaced = V4L2_DV_INTERLACED;
	}

	fmt->bt.pixelclock = pix_clk;
	fmt->bt.standards = V4L2_DV_BT_STD_CVT;

	if (reduced_blanking)
		fmt->bt.flags |= V4L2_DV_FL_REDUCED_BLANKING;

	return true;
}
EXPORT_SYMBOL_GPL(v4l2_detect_cvt);

/*
 * GTF defines
 * Based on Generalized Timing Formula Standard
 * Version 1.1 September 2, 1999
 */

#define GTF_PXL_CLK_GRAN	250000	/* pixel clock granularity */

#define GTF_MIN_VSYNC_BP	550	/* min time of vsync + back porch (us) */
#define GTF_V_FP		1	/* vertical front porch (lines) */
#define GTF_CELL_GRAN		8	/* character cell granularity */

/* Default */
#define GTF_D_M			600	/* blanking formula gradient */
#define GTF_D_C			40	/* blanking formula offset */
#define GTF_D_K			128	/* blanking formula scaling factor */
#define GTF_D_J			20	/* blanking formula scaling factor */
#define GTF_D_C_PRIME ((((GTF_D_C - GTF_D_J) * GTF_D_K) / 256) + GTF_D_J)
#define GTF_D_M_PRIME ((GTF_D_K * GTF_D_M) / 256)

/* Secondary */
#define GTF_S_M			3600	/* blanking formula gradient */
#define GTF_S_C			40	/* blanking formula offset */
#define GTF_S_K			128	/* blanking formula scaling factor */
#define GTF_S_J			35	/* blanking formula scaling factor */
#define GTF_S_C_PRIME ((((GTF_S_C - GTF_S_J) * GTF_S_K) / 256) + GTF_S_J)
#define GTF_S_M_PRIME ((GTF_S_K * GTF_S_M) / 256)

/** v4l2_detect_gtf - detect if the given timings follow the GTF standard
 * @frame_height - the total height of the frame (including blanking) in lines.
 * @hfreq - the horizontal frequency in Hz.
 * @vsync - the height of the vertical sync in lines.
 * @polarities - the horizontal and vertical polarities (same as struct
 *		v4l2_bt_timings polarities).
 * @interlaced - if this flag is true, it indicates interlaced format
 * @aspect - preferred aspect ratio. GTF has no method of determining the
 *		aspect ratio in order to derive the image width from the
 *		image height, so it has to be passed explicitly. Usually
 *		the native screen aspect ratio is used for this. If it
 *		is not filled in correctly, then 16:9 will be assumed.
 * @fmt - the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid GTF format. If so, then it will return true, and fmt will be filled
 * in with the found GTF timings.
 */
bool v4l2_detect_gtf(unsigned frame_height,
		unsigned hfreq,
		unsigned vsync,
		u32 polarities,
		bool interlaced,
		struct v4l2_fract aspect,
		struct v4l2_dv_timings *fmt)
{
	int pix_clk;
	int  v_fp, v_bp, h_fp, hsync;
	int frame_width, image_height, image_width;
	bool default_gtf;
	int h_blank;

	if (vsync != 3)
		return false;

	if (polarities == V4L2_DV_VSYNC_POS_POL)
		default_gtf = true;
	else if (polarities == V4L2_DV_HSYNC_POS_POL)
		default_gtf = false;
	else
		return false;

	if (hfreq == 0)
		return false;

	/* Vertical */
	v_fp = GTF_V_FP;
	v_bp = (GTF_MIN_VSYNC_BP * hfreq + 500000) / 1000000 - vsync;
	if (interlaced)
		image_height = (frame_height - 2 * v_fp - 2 * vsync - 2 * v_bp) & ~0x1;
	else
		image_height = (frame_height - v_fp - vsync - v_bp + 1) & ~0x1;

	if (image_height < 0)
		return false;

	if (aspect.numerator == 0 || aspect.denominator == 0) {
		aspect.numerator = 16;
		aspect.denominator = 9;
	}
	image_width = ((image_height * aspect.numerator) / aspect.denominator);
	image_width = (image_width + GTF_CELL_GRAN/2) & ~(GTF_CELL_GRAN - 1);

	/* Horizontal */
	if (default_gtf) {
		u64 num;
		u32 den;

		num = ((image_width * GTF_D_C_PRIME * (u64)hfreq) -
		      ((u64)image_width * GTF_D_M_PRIME * 1000));
		den = (hfreq * (100 - GTF_D_C_PRIME) + GTF_D_M_PRIME * 1000) *
		      (2 * GTF_CELL_GRAN);
		h_blank = div_u64((num + (den >> 1)), den);
		h_blank *= (2 * GTF_CELL_GRAN);
	} else {
		u64 num;
		u32 den;

		num = ((image_width * GTF_S_C_PRIME * (u64)hfreq) -
		      ((u64)image_width * GTF_S_M_PRIME * 1000));
		den = (hfreq * (100 - GTF_S_C_PRIME) + GTF_S_M_PRIME * 1000) *
		      (2 * GTF_CELL_GRAN);
		h_blank = div_u64((num + (den >> 1)), den);
		h_blank *= (2 * GTF_CELL_GRAN);
	}

	frame_width = image_width + h_blank;

	pix_clk = (image_width + h_blank) * hfreq;
	pix_clk = pix_clk / GTF_PXL_CLK_GRAN * GTF_PXL_CLK_GRAN;

	hsync = (frame_width * 8 + 50) / 100;
	hsync = ((hsync + GTF_CELL_GRAN / 2) / GTF_CELL_GRAN) * GTF_CELL_GRAN;

	h_fp = h_blank / 2 - hsync;

	fmt->type = V4L2_DV_BT_656_1120;
	fmt->bt.polarities = polarities;
	fmt->bt.width = image_width;
	fmt->bt.height = image_height;
	fmt->bt.hfrontporch = h_fp;
	fmt->bt.vfrontporch = v_fp;
	fmt->bt.hsync = hsync;
	fmt->bt.vsync = vsync;
	fmt->bt.hbackporch = frame_width - image_width - h_fp - hsync;

	if (!interlaced) {
		fmt->bt.vbackporch = frame_height - image_height - v_fp - vsync;
		fmt->bt.interlaced = V4L2_DV_PROGRESSIVE;
	} else {
		fmt->bt.vbackporch = (frame_height - image_height - 2 * v_fp -
				      2 * vsync) / 2;
		fmt->bt.il_vbackporch = frame_height - image_height - 2 * v_fp -
					2 * vsync - fmt->bt.vbackporch;
		fmt->bt.il_vfrontporch = v_fp;
		fmt->bt.il_vsync = vsync;
		fmt->bt.flags |= V4L2_DV_FL_HALF_LINE;
		fmt->bt.interlaced = V4L2_DV_INTERLACED;
	}

	fmt->bt.pixelclock = pix_clk;
	fmt->bt.standards = V4L2_DV_BT_STD_GTF;

	if (!default_gtf)
		fmt->bt.flags |= V4L2_DV_FL_REDUCED_BLANKING;

	return true;
}
EXPORT_SYMBOL_GPL(v4l2_detect_gtf);

/** v4l2_calc_aspect_ratio - calculate the aspect ratio based on bytes
 *	0x15 and 0x16 from the EDID.
 * @hor_landscape - byte 0x15 from the EDID.
 * @vert_portrait - byte 0x16 from the EDID.
 *
 * Determines the aspect ratio from the EDID.
 * See VESA Enhanced EDID standard, release A, rev 2, section 3.6.2:
 * "Horizontal and Vertical Screen Size or Aspect Ratio"
 */
struct v4l2_fract v4l2_calc_aspect_ratio(u8 hor_landscape, u8 vert_portrait)
{
	struct v4l2_fract aspect = { 16, 9 };
	u8 ratio;

	/* Nothing filled in, fallback to 16:9 */
	if (!hor_landscape && !vert_portrait)
		return aspect;
	/* Both filled in, so they are interpreted as the screen size in cm */
	if (hor_landscape && vert_portrait) {
		aspect.numerator = hor_landscape;
		aspect.denominator = vert_portrait;
		return aspect;
	}
	/* Only one is filled in, so interpret them as a ratio:
	   (val + 99) / 100 */
	ratio = hor_landscape | vert_portrait;
	/* Change some rounded values into the exact aspect ratio */
	if (ratio == 79) {
		aspect.numerator = 16;
		aspect.denominator = 9;
	} else if (ratio == 34) {
		aspect.numerator = 4;
		aspect.denominator = 3;
	} else if (ratio == 68) {
		aspect.numerator = 15;
		aspect.denominator = 9;
	} else {
		aspect.numerator = hor_landscape + 99;
		aspect.denominator = 100;
	}
	if (hor_landscape)
		return aspect;
	/* The aspect ratio is for portrait, so swap numerator and denominator */
	swap(aspect.denominator, aspect.numerator);
	return aspect;
}
EXPORT_SYMBOL_GPL(v4l2_calc_aspect_ratio);
