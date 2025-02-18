
/*
 * drm_sysfs.c - Modifications to drm_sysfs_class.c to support
 *               extra sysfs attribute from DRM. Normal drm_sysfs_class
 *               does not allow adding attributes.
 *
 * Copyright (c) 2004 Jon Smirl <jonsmirl@gmail.com>
 * Copyright (C) 2021 XiaoMi, Inc.
 * Copyright (c) 2003-2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2003-2004 IBM Corp.
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/export.h>

#include <drm/drm_sysfs.h>
#include <drm/drmP.h>
#include "drm_internal.h"
//#include "mediatek/mtk_drm_ddp_comp.h"
//struct mtk_dsi;
#define to_drm_minor(d) dev_get_drvdata(d)
#define to_drm_connector(d) dev_get_drvdata(d)
//#define to_mtk_dsi(x)  container_of(x, struct mtk_dsi, conn)

#define PANEL_MAX_LUMINANCE_READ_OFFSET 12

/**
 * DOC: overview
 *
 * DRM provides very little additional support to drivers for sysfs
 * interactions, beyond just all the standard stuff. Drivers who want to expose
 * additional sysfs properties and property groups can attach them at either
 * &drm_device.dev or &drm_connector.kdev.
 *
 * Registration is automatically handled when calling drm_dev_register(), or
 * drm_connector_register() in case of hot-plugged connectors. Unregistration is
 * also automatically handled by drm_dev_unregister() and
 * drm_connector_unregister().
 */

static struct device_type drm_sysfs_device_minor = {
	.name = "drm_minor"
};

struct class *drm_class;

static char *drm_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "dri/%s", dev_name(dev));
}

static CLASS_ATTR_STRING(version, S_IRUGO, "drm 1.1.0 20060810");

/**
 * drm_sysfs_init - initialize sysfs helpers
 *
 * This is used to create the DRM class, which is the implicit parent of any
 * other top-level DRM sysfs objects.
 *
 * You must call drm_sysfs_destroy() to release the allocated resources.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_sysfs_init(void)
{
	int err;

	drm_class = class_create(THIS_MODULE, "drm");
	if (IS_ERR(drm_class))
		return PTR_ERR(drm_class);

	err = class_create_file(drm_class, &class_attr_version.attr);
	if (err) {
		class_destroy(drm_class);
		drm_class = NULL;
		return err;
	}

	drm_class->devnode = drm_devnode;
	return 0;
}

/**
 * drm_sysfs_destroy - destroys DRM class
 *
 * Destroy the DRM device class.
 */
void drm_sysfs_destroy(void)
{
	if (IS_ERR_OR_NULL(drm_class))
		return;
	class_remove_file(drm_class, &class_attr_version.attr);
	class_destroy(drm_class);
	drm_class = NULL;
}

/*
 * Connector properties
 */
static ssize_t status_store(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	enum drm_connector_force old_force;
	int ret;

	ret = mutex_lock_interruptible(&dev->mode_config.mutex);
	if (ret)
		return ret;

	old_force = connector->force;

	if (sysfs_streq(buf, "detect"))
		connector->force = 0;
	else if (sysfs_streq(buf, "on"))
		connector->force = DRM_FORCE_ON;
	else if (sysfs_streq(buf, "on-digital"))
		connector->force = DRM_FORCE_ON_DIGITAL;
	else if (sysfs_streq(buf, "off"))
		connector->force = DRM_FORCE_OFF;
	else
		ret = -EINVAL;

	if (old_force != connector->force || !connector->force) {
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] force updated from %d to %d or reprobing\n",
			      connector->base.id,
			      connector->name,
			      old_force, connector->force);

		connector->funcs->fill_modes(connector,
					     dev->mode_config.max_width,
					     dev->mode_config.max_height);
	}

	mutex_unlock(&dev->mode_config.mutex);

	return ret ? ret : count;
}

static ssize_t status_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	enum drm_connector_status status;

	status = READ_ONCE(connector->status);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			drm_get_connector_status_name(status));
}

static ssize_t dpms_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	int dpms;

	dpms = READ_ONCE(connector->dpms);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			drm_get_dpms_name(dpms));
}

static ssize_t enabled_show(struct device *device,
			    struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	bool enabled;

	enabled = READ_ONCE(connector->encoder);

	return snprintf(buf, PAGE_SIZE, enabled ? "enabled\n" : "disabled\n");
}

static ssize_t edid_show(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf, loff_t off,
			 size_t count)
{
	struct device *connector_dev = kobj_to_dev(kobj);
	struct drm_connector *connector = to_drm_connector(connector_dev);
	unsigned char *edid;
	size_t size;
	ssize_t ret = 0;

	mutex_lock(&connector->dev->mode_config.mutex);
	if (!connector->edid_blob_ptr)
		goto unlock;

	edid = connector->edid_blob_ptr->data;
	size = connector->edid_blob_ptr->length;
	if (!edid)
		goto unlock;

	if (off >= size)
		goto unlock;

	if (off + count > size)
		count = size - off;
	memcpy(buf, edid + off, count);

	ret = count;
unlock:
	mutex_unlock(&connector->dev->mode_config.mutex);

	return ret;
}

static ssize_t modes_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_display_mode *mode;
	int written = 0;

	mutex_lock(&connector->dev->mode_config.mutex);
	list_for_each_entry(mode, &connector->modes, head) {
		written += snprintf(buf + written, PAGE_SIZE - written, "%s\n",
				    mode->name);
	}
	mutex_unlock(&connector->dev->mode_config.mutex);

	return written;
}

static ssize_t panel_info_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
    int ret = 0;
    const char *panel_name_prefix = "panel_name=dsi_";
    struct drm_connector *connector = to_drm_connector(device);
    if (!connector) {
        pr_debug("%s-%d connector is NULL \r\n",__func__, __LINE__);
        return ret;
    }

    return snprintf(buf, PAGE_SIZE, "%s%s\n", panel_name_prefix, connector->display_info.name);
}

extern ssize_t panel_disp_param_send_lock(struct drm_connector* connector, int32_t param);
static ssize_t disp_param_store(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	ssize_t ret = 0;
	int32_t param;
	struct drm_connector *connector = to_drm_connector(device);
	if (!connector) {
		pr_debug("%s-%d connector is NULL \r\n",__func__, __LINE__);
		return ret;
	}

	sscanf(buf, "0x%x", &param);
	ret = panel_disp_param_send_lock(connector, param);
	return count;
}

static ssize_t disp_param_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
    ssize_t ret = 0;
    unsigned char* p_temp;
    struct drm_connector *connector = to_drm_connector(device);
    if (!connector) {
        pr_debug("%s-%d connector is NULL\r\n",__func__, __LINE__);
        return ret;
    }

    p_temp =  connector->panel_read_data;

	if (connector->read_flag == 0x1) {
		ret = scnprintf(buf, 13, "%s", p_temp);
		pr_debug("dsi panel read data flag: 0x1, buf: %s\n", buf);
	} else if (connector->read_flag == 0x2) {
		ret = scnprintf(buf, 7, "%s", p_temp + PANEL_MAX_LUMINANCE_READ_OFFSET );
		pr_debug("dsi panel read data flag: 0x2, buf: %s\n", buf);
	} else {
		pr_debug("dai panel read data flag 0x%x can't use !\n", connector->read_flag);
	}
	
    return ret;
}

static ssize_t fod_ui_ready_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	return snprintf(buf, PAGE_SIZE, "%d\n", connector->fod_ui_ready);
}

static ssize_t panel_id_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	ssize_t ret = 0;
	struct drm_connector *connector = to_drm_connector(device);
	if (!connector) {
		pr_debug("%s-%d connector is NULL \r\n",__func__, __LINE__);
		return ret;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", connector->panel_id);
}

extern ssize_t lcm_mipi_reg_write(char *buf, size_t count);
extern ssize_t lcm_mipi_reg_read(char *buf);

static ssize_t mipi_reg_show(struct device *device,
			    struct device_attribute *attr,
			   char *buf)
{
	return lcm_mipi_reg_read(buf);
}

static ssize_t mipi_reg_store(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int rc = 0;
	rc = lcm_mipi_reg_write((char *)buf, count);
	return rc;
}

static ssize_t panel_event_show(struct device *device,
                           struct device_attribute *attr,
                           char *buf)
{
        ssize_t ret = 0;
        struct drm_connector *connector = to_drm_connector(device);
        if (!connector) {
                pr_debug("%s-%d connector is NULL \r\n",__func__, __LINE__);
                return ret;
        }

        return snprintf(buf, PAGE_SIZE, "%d\n", connector->panel_event);
}

extern ssize_t dsi_display_set_doze_brightness(struct drm_connector *connector, int doze_brightness);
extern ssize_t dsi_display_get_doze_brightness(struct drm_connector *connector, char *buf);

static ssize_t doze_brightness_store(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int doze_brightness;
	int ret;
	struct drm_connector *connector = to_drm_connector(device);
	pr_debug("%s +\n", __func__);
	if (!connector) {
		pr_err("%s, the connector is null\n", __func__);
		return 0;
	}

	ret = kstrtoint(buf, 0, &doze_brightness);;
	if (ret)
		return ret;

	ret = dsi_display_set_doze_brightness(connector, doze_brightness);

	return ret ? ret : count;
}

static ssize_t doze_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);

	if (!connector) {
		pr_err("%s, the connector is null\n", __func__);
		return 0;
	}

	return dsi_display_get_doze_brightness(connector, buf);
}

extern int dsi_display_panel_set_thermal_hbm_disabled(struct drm_connector *connector, bool thermal_hbm_disabled);
extern int dsi_display_panel_get_thermal_hbm_disabled(struct drm_connector *connector, bool *thermal_hbm_disabled);

static ssize_t thermal_hbm_disabled_store(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(device);
	char *input_copy, *input_dup = NULL;
	bool thermal_hbm_disabled;
	int ret;

	input_copy = kstrdup(buf, GFP_KERNEL);
	if (!input_copy) {
		pr_err("can not allocate memory\n");
		ret = -ENOMEM;
		goto exit;
	}
	input_dup = input_copy;
	/* removes leading and trailing whitespace from input_copy */
	input_copy = strim(input_copy);
	ret = kstrtobool(input_copy, &thermal_hbm_disabled);
	if (ret) {
		pr_err("input buffer conversion failed\n");
		ret = -EAGAIN;
		goto exit_free;
	}

	pr_debug("set thermal_hbm_disabled %d\n", thermal_hbm_disabled);
	ret = dsi_display_panel_set_thermal_hbm_disabled(connector, thermal_hbm_disabled);

exit_free:
	kfree(input_dup);
exit:
	return ret ? ret : count;
}

static ssize_t thermal_hbm_disabled_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	bool thermal_hbm_disabled;

	dsi_display_panel_get_thermal_hbm_disabled(connector, &thermal_hbm_disabled);

	return snprintf(buf, PAGE_SIZE, "%d\n", thermal_hbm_disabled);
}

static DEVICE_ATTR_RW(status);
static DEVICE_ATTR_RO(enabled);
static DEVICE_ATTR_RO(dpms);
static DEVICE_ATTR_RO(modes);
static DEVICE_ATTR_RW(disp_param);
static DEVICE_ATTR_RO(panel_info);
static DEVICE_ATTR_RO(fod_ui_ready);
static DEVICE_ATTR_RO(panel_id);
static DEVICE_ATTR_RW(mipi_reg);
static DEVICE_ATTR_RO(panel_event);
static DEVICE_ATTR_RW(doze_brightness);
static DEVICE_ATTR_RW(thermal_hbm_disabled);

static struct attribute *connector_dev_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_enabled.attr,
	&dev_attr_dpms.attr,
	&dev_attr_modes.attr,
	&dev_attr_disp_param.attr,
	&dev_attr_panel_info.attr,
	&dev_attr_fod_ui_ready.attr,
	&dev_attr_panel_id.attr,
	&dev_attr_mipi_reg.attr,
	&dev_attr_panel_event.attr,
	&dev_attr_doze_brightness.attr,
	&dev_attr_thermal_hbm_disabled.attr,
	NULL
};

static struct bin_attribute edid_attr = {
	.attr.name = "edid",
	.attr.mode = 0444,
	.size = 0,
	.read = edid_show,
};

static struct bin_attribute *connector_bin_attrs[] = {
	&edid_attr,
	NULL
};

static const struct attribute_group connector_dev_group = {
	.attrs = connector_dev_attrs,
	.bin_attrs = connector_bin_attrs,
};

static const struct attribute_group *connector_dev_groups[] = {
	&connector_dev_group,
	NULL
};

int drm_sysfs_connector_add(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	if (connector->kdev)
		return 0;

	connector->kdev =
		device_create_with_groups(drm_class, dev->primary->kdev, 0,
					  connector, connector_dev_groups,
					  "card%d-%s", dev->primary->index,
					  connector->name);
	DRM_DEBUG("adding \"%s\" to sysfs\n",
		  connector->name);

	if (IS_ERR(connector->kdev)) {
		DRM_ERROR("failed to register connector device: %ld\n", PTR_ERR(connector->kdev));
		return PTR_ERR(connector->kdev);
	}

	/* Let userspace know we have a new connector */
	drm_sysfs_hotplug_event(dev);

	return 0;
}

void drm_sysfs_connector_remove(struct drm_connector *connector)
{
	if (!connector->kdev)
		return;
	DRM_DEBUG("removing \"%s\" from sysfs\n",
		  connector->name);

	device_unregister(connector->kdev);
	connector->kdev = NULL;
}

/**
 * drm_sysfs_hotplug_event - generate a DRM uevent
 * @dev: DRM device
 *
 * Send a uevent for the DRM device specified by @dev.  Currently we only
 * set HOTPLUG=1 in the uevent environment, but this could be expanded to
 * deal with other types of events.
 */
void drm_sysfs_hotplug_event(struct drm_device *dev)
{
	char *event_string = "HOTPLUG=1";
	char *envp[] = { event_string, NULL };

	DRM_DEBUG("generating hotplug event\n");

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(drm_sysfs_hotplug_event);

static void drm_sysfs_release(struct device *dev)
{
	kfree(dev);
}

struct device *drm_sysfs_minor_alloc(struct drm_minor *minor)
{
	const char *minor_str;
	struct device *kdev;
	int r;

	if (minor->type == DRM_MINOR_CONTROL)
		minor_str = "controlD%d";
	else if (minor->type == DRM_MINOR_RENDER)
		minor_str = "renderD%d";
	else
		minor_str = "card%d";

	kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
	if (!kdev)
		return ERR_PTR(-ENOMEM);

	device_initialize(kdev);
	kdev->devt = MKDEV(DRM_MAJOR, minor->index);
	kdev->class = drm_class;
	kdev->type = &drm_sysfs_device_minor;
	kdev->parent = minor->dev->dev;
	kdev->release = drm_sysfs_release;
	dev_set_drvdata(kdev, minor);

	r = dev_set_name(kdev, minor_str, minor->index);
	if (r < 0)
		goto err_free;

	return kdev;

err_free:
	put_device(kdev);
	return ERR_PTR(r);
}

/**
 * drm_class_device_register - register new device with the DRM sysfs class
 * @dev: device to register
 *
 * Registers a new &struct device within the DRM sysfs class. Essentially only
 * used by ttm to have a place for its global settings. Drivers should never use
 * this.
 */
int drm_class_device_register(struct device *dev)
{
	if (!drm_class || IS_ERR(drm_class))
		return -ENOENT;

	dev->class = drm_class;
	return device_register(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_register);

/**
 * drm_class_device_unregister - unregister device with the DRM sysfs class
 * @dev: device to unregister
 *
 * Unregisters a &struct device from the DRM sysfs class. Essentially only used
 * by ttm to have a place for its global settings. Drivers should never use
 * this.
 */
void drm_class_device_unregister(struct device *dev)
{
	return device_unregister(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_unregister);
