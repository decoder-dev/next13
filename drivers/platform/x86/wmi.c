/*
 *  ACPI-WMI mapping driver
 *
 *  Copyright (C) 2007-2008 Carlos Corbacho <carlos@strangeworlds.co.uk>
 *
 *  GUID parsing code from ldm.c is:
 *   Copyright (C) 2001,2002 Richard Russon <ldm@flatcap.org>
 *   Copyright (c) 2001-2007 Anton Altaparmakov
 *   Copyright (C) 2001,2002 Jakob Kemi <jakob.kemi@telia.com>
 *
 *  WMI bus infrastructure by Andrew Lutomirski and Darren Hart:
 *    Copyright (C) 2015 Andrew Lutomirski
 *    Copyright (C) 2017 VMware, Inc. All Rights Reserved.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/wmi.h>
#include <linux/uuid.h>

ACPI_MODULE_NAME("wmi");
MODULE_AUTHOR("Carlos Corbacho");
MODULE_DESCRIPTION("ACPI-WMI Mapping Driver");
MODULE_LICENSE("GPL");

static LIST_HEAD(wmi_block_list);

struct guid_block {
	char guid[16];
	union {
		char object_id[2];
		struct {
			unsigned char notify_id;
			unsigned char reserved;
		};
	};
	u8 instance_count;
	u8 flags;
};

struct wmi_block {
	struct wmi_device dev;
	struct list_head list;
	struct guid_block gblock;
	struct acpi_device *acpi_device;
	wmi_notify_handler handler;
	void *handler_data;

	bool read_takes_no_args;
};


/*
 * If the GUID data block is marked as expensive, we must enable and
 * explicitily disable data collection.
 */
#define ACPI_WMI_EXPENSIVE   0x1
#define ACPI_WMI_METHOD      0x2	/* GUID is a method */
#define ACPI_WMI_STRING      0x4	/* GUID takes & returns a string */
#define ACPI_WMI_EVENT       0x8	/* GUID is an event */

static bool debug_event;
module_param(debug_event, bool, 0444);
MODULE_PARM_DESC(debug_event,
		 "Log WMI Events [0/1]");

static bool debug_dump_wdg;
module_param(debug_dump_wdg, bool, 0444);
MODULE_PARM_DESC(debug_dump_wdg,
		 "Dump available WMI interfaces [0/1]");

static int acpi_wmi_remove(struct platform_device *device);
static int acpi_wmi_probe(struct platform_device *device);

static const struct acpi_device_id wmi_device_ids[] = {
	{"PNP0C14", 0},
	{"pnp0c14", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, wmi_device_ids);

static struct platform_driver acpi_wmi_driver = {
	.driver = {
		.name = "acpi-wmi",
		.acpi_match_table = wmi_device_ids,
	},
	.probe = acpi_wmi_probe,
	.remove = acpi_wmi_remove,
};

/*
 * GUID parsing functions
 */

static bool find_guid(const char *guid_string, struct wmi_block **out)
{
	uuid_le guid_input;
	struct wmi_block *wblock;
	struct guid_block *block;
	struct list_head *p;

	if (uuid_le_to_bin(guid_string, &guid_input))
		return false;

	list_for_each(p, &wmi_block_list) {
		wblock = list_entry(p, struct wmi_block, list);
		block = &wblock->gblock;

		if (memcmp(block->guid, &guid_input, 16) == 0) {
			if (out)
				*out = wblock;
			return true;
		}
	}
	return false;
}

static int get_subobj_info(acpi_handle handle, const char *pathname,
			   struct acpi_device_info **info)
{
	struct acpi_device_info *dummy_info, **info_ptr;
	acpi_handle subobj_handle;
	acpi_status status;

	status = acpi_get_handle(handle, (char *)pathname, &subobj_handle);
	if (status == AE_NOT_FOUND)
		return -ENOENT;
	else if (ACPI_FAILURE(status))
		return -EIO;

	info_ptr = info ? info : &dummy_info;
	status = acpi_get_object_info(subobj_handle, info_ptr);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (!info)
		kfree(dummy_info);

	return 0;
}

static acpi_status wmi_method_enable(struct wmi_block *wblock, int enable)
{
	struct guid_block *block = NULL;
	char method[5];
	acpi_status status;
	acpi_handle handle;

	block = &wblock->gblock;
	handle = wblock->acpi_device->handle;

	snprintf(method, 5, "WE%02X", block->notify_id);
	status = acpi_execute_simple_method(handle, method, enable);

	if (status != AE_OK && status != AE_NOT_FOUND)
		return status;
	else
		return AE_OK;
}

/*
 * Exported WMI functions
 */
/**
 * wmi_evaluate_method - Evaluate a WMI method
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @instance: Instance index
 * @method_id: Method ID to call
 * &in: Buffer containing input for the method call
 * &out: Empty buffer to return the method results
 *
 * Call an ACPI-WMI method
 */
acpi_status wmi_evaluate_method(const char *guid_string, u8 instance,
u32 method_id, const struct acpi_buffer *in, struct acpi_buffer *out)
{
	struct guid_block *block = NULL;
	struct wmi_block *wblock = NULL;
	acpi_handle handle;
	acpi_status status;
	struct acpi_object_list input;
	union acpi_object params[3];
	char method[5] = "WM";

	if (!find_guid(guid_string, &wblock))
		return AE_ERROR;

	block = &wblock->gblock;
	handle = wblock->acpi_device->handle;

	if (!(block->flags & ACPI_WMI_METHOD))
		return AE_BAD_DATA;

	if (block->instance_count <= instance)
		return AE_BAD_PARAMETER;

	input.count = 2;
	input.pointer = params;
	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = instance;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = method_id;

	if (in) {
		input.count = 3;

		if (block->flags & ACPI_WMI_STRING) {
			params[2].type = ACPI_TYPE_STRING;
		} else {
			params[2].type = ACPI_TYPE_BUFFER;
		}
		params[2].buffer.length = in->length;
		params[2].buffer.pointer = in->pointer;
	}

	strncat(method, block->object_id, 2);

	status = acpi_evaluate_object(handle, method, &input, out);

	return status;
}
EXPORT_SYMBOL_GPL(wmi_evaluate_method);

static acpi_status __query_block(struct wmi_block *wblock, u8 instance,
				 struct acpi_buffer *out)
{
	struct guid_block *block = NULL;
	acpi_handle handle;
	acpi_status status, wc_status = AE_ERROR;
	struct acpi_object_list input;
	union acpi_object wq_params[1];
	char method[5];
	char wc_method[5] = "WC";

	if (!out)
		return AE_BAD_PARAMETER;

	block = &wblock->gblock;
	handle = wblock->acpi_device->handle;

	if (block->instance_count <= instance)
		return AE_BAD_PARAMETER;

	/* Check GUID is a data block */
	if (block->flags & (ACPI_WMI_EVENT | ACPI_WMI_METHOD))
		return AE_ERROR;

	input.count = 1;
	input.pointer = wq_params;
	wq_params[0].type = ACPI_TYPE_INTEGER;
	wq_params[0].integer.value = instance;

	if (instance == 0 && wblock->read_takes_no_args)
		input.count = 0;

	/*
	 * If ACPI_WMI_EXPENSIVE, call the relevant WCxx method first to
	 * enable collection.
	 */
	if (block->flags & ACPI_WMI_EXPENSIVE) {
		strncat(wc_method, block->object_id, 2);

		/*
		 * Some GUIDs break the specification by declaring themselves
		 * expensive, but have no corresponding WCxx method. So we
		 * should not fail if this happens.
		 */
		if (acpi_has_method(handle, wc_method))
			wc_status = acpi_execute_simple_method(handle,
								wc_method, 1);
	}

	strcpy(method, "WQ");
	strncat(method, block->object_id, 2);

	status = acpi_evaluate_object(handle, method, &input, out);

	/*
	 * If ACPI_WMI_EXPENSIVE, call the relevant WCxx method, even if
	 * the WQxx method failed - we should disable collection anyway.
	 */
	if ((block->flags & ACPI_WMI_EXPENSIVE) && ACPI_SUCCESS(wc_status)) {
		/*
		 * Ignore whether this WCxx call succeeds or not since
		 * the previously executed WQxx method call might have
		 * succeeded, and returning the failing status code
		 * of this call would throw away the result of the WQxx
		 * call, potentially leaking memory.
		 */
		acpi_execute_simple_method(handle, wc_method, 0);
	}

	return status;
}

/**
 * wmi_query_block - Return contents of a WMI block (deprecated)
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @instance: Instance index
 * &out: Empty buffer to return the contents of the data block to
 *
 * Return the contents of an ACPI-WMI data block to a buffer
 */
acpi_status wmi_query_block(const char *guid_string, u8 instance,
			    struct acpi_buffer *out)
{
	struct wmi_block *wblock;

	if (!guid_string)
		return AE_BAD_PARAMETER;

	if (!find_guid(guid_string, &wblock))
		return AE_ERROR;

	return __query_block(wblock, instance, out);
}
EXPORT_SYMBOL_GPL(wmi_query_block);

union acpi_object *wmidev_block_query(struct wmi_device *wdev, u8 instance)
{
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	struct wmi_block *wblock = container_of(wdev, struct wmi_block, dev);

	if (ACPI_FAILURE(__query_block(wblock, instance, &out)))
		return NULL;

	return (union acpi_object *)out.pointer;
}
EXPORT_SYMBOL_GPL(wmidev_block_query);

struct wmi_device *wmidev_get_other_guid(struct wmi_device *wdev,
					 const char *guid_string)
{
	struct wmi_block *this_wb = container_of(wdev, struct wmi_block, dev);
	struct wmi_block *other_wb;

	if (!find_guid(guid_string, &other_wb))
		return NULL;

	if (other_wb->acpi_device != this_wb->acpi_device)
		return NULL;

	get_device(&other_wb->dev.dev);
	return &other_wb->dev;
}
EXPORT_SYMBOL_GPL(wmidev_get_other_guid);

/**
 * wmi_set_block - Write to a WMI block
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @instance: Instance index
 * &in: Buffer containing new values for the data block
 *
 * Write the contents of the input buffer to an ACPI-WMI data block
 */
acpi_status wmi_set_block(const char *guid_string, u8 instance,
			  const struct acpi_buffer *in)
{
	struct guid_block *block = NULL;
	struct wmi_block *wblock = NULL;
	acpi_handle handle;
	struct acpi_object_list input;
	union acpi_object params[2];
	char method[5] = "WS";

	if (!guid_string || !in)
		return AE_BAD_DATA;

	if (!find_guid(guid_string, &wblock))
		return AE_ERROR;

	block = &wblock->gblock;
	handle = wblock->acpi_device->handle;

	if (block->instance_count <= instance)
		return AE_BAD_PARAMETER;

	/* Check GUID is a data block */
	if (block->flags & (ACPI_WMI_EVENT | ACPI_WMI_METHOD))
		return AE_ERROR;

	input.count = 2;
	input.pointer = params;
	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = instance;

	if (block->flags & ACPI_WMI_STRING) {
		params[1].type = ACPI_TYPE_STRING;
	} else {
		params[1].type = ACPI_TYPE_BUFFER;
	}
	params[1].buffer.length = in->length;
	params[1].buffer.pointer = in->pointer;

	strncat(method, block->object_id, 2);

	return acpi_evaluate_object(handle, method, &input, NULL);
}
EXPORT_SYMBOL_GPL(wmi_set_block);

static void wmi_dump_wdg(const struct guid_block *g)
{
	pr_debug("%pUL:\n", g->guid);
	if (g->flags & ACPI_WMI_EVENT)
		pr_debug("\tnotify_id: 0x%02X\n", g->notify_id);
	else
		pr_debug("\tobject_id: %2pE\n", g->object_id);
	pr_debug("\tinstance_count: %d\n", g->instance_count);
	pr_debug("\tflags: %#x", g->flags);
	if (g->flags) {
		if (g->flags & ACPI_WMI_EXPENSIVE)
			pr_cont(" ACPI_WMI_EXPENSIVE");
		if (g->flags & ACPI_WMI_METHOD)
			pr_cont(" ACPI_WMI_METHOD");
		if (g->flags & ACPI_WMI_STRING)
			pr_cont(" ACPI_WMI_STRING");
		if (g->flags & ACPI_WMI_EVENT)
			pr_cont(" ACPI_WMI_EVENT");
	}
	pr_cont("\n");

}

static void wmi_notify_debug(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_debug("bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;

	if (!obj)
		return;

	pr_debug("DEBUG Event ");
	switch(obj->type) {
	case ACPI_TYPE_BUFFER:
		pr_cont("BUFFER_TYPE - length %d\n", obj->buffer.length);
		break;
	case ACPI_TYPE_STRING:
		pr_cont("STRING_TYPE - %s\n", obj->string.pointer);
		break;
	case ACPI_TYPE_INTEGER:
		pr_cont("INTEGER_TYPE - %llu\n", obj->integer.value);
		break;
	case ACPI_TYPE_PACKAGE:
		pr_cont("PACKAGE_TYPE - %d elements\n", obj->package.count);
		break;
	default:
		pr_cont("object type 0x%X\n", obj->type);
	}
	kfree(obj);
}

/**
 * wmi_install_notify_handler - Register handler for WMI events
 * @handler: Function to handle notifications
 * @data: Data to be returned to handler when event is fired
 *
 * Register a handler for events sent to the ACPI-WMI mapper device.
 */
acpi_status wmi_install_notify_handler(const char *guid,
wmi_notify_handler handler, void *data)
{
	struct wmi_block *block;
	acpi_status status = AE_NOT_EXIST;
	uuid_le guid_input;
	struct list_head *p;

	if (!guid || !handler)
		return AE_BAD_PARAMETER;

	if (uuid_le_to_bin(guid, &guid_input))
		return AE_BAD_PARAMETER;

	list_for_each(p, &wmi_block_list) {
		acpi_status wmi_status;
		block = list_entry(p, struct wmi_block, list);

		if (memcmp(block->gblock.guid, &guid_input, 16) == 0) {
			if (block->handler &&
			    block->handler != wmi_notify_debug)
				return AE_ALREADY_ACQUIRED;

			block->handler = handler;
			block->handler_data = data;

			wmi_status = wmi_method_enable(block, 1);
			if ((wmi_status != AE_OK) ||
			    ((wmi_status == AE_OK) && (status == AE_NOT_EXIST)))
				status = wmi_status;
		}
	}

	return status;
}
EXPORT_SYMBOL_GPL(wmi_install_notify_handler);

/**
 * wmi_uninstall_notify_handler - Unregister handler for WMI events
 *
 * Unregister handler for events sent to the ACPI-WMI mapper device.
 */
acpi_status wmi_remove_notify_handler(const char *guid)
{
	struct wmi_block *block;
	acpi_status status = AE_NOT_EXIST;
	uuid_le guid_input;
	struct list_head *p;

	if (!guid)
		return AE_BAD_PARAMETER;

	if (uuid_le_to_bin(guid, &guid_input))
		return AE_BAD_PARAMETER;

	list_for_each(p, &wmi_block_list) {
		acpi_status wmi_status;
		block = list_entry(p, struct wmi_block, list);

		if (memcmp(block->gblock.guid, &guid_input, 16) == 0) {
			if (!block->handler ||
			    block->handler == wmi_notify_debug)
				return AE_NULL_ENTRY;

			if (debug_event) {
				block->handler = wmi_notify_debug;
				status = AE_OK;
			} else {
				wmi_status = wmi_method_enable(block, 0);
				block->handler = NULL;
				block->handler_data = NULL;
				if ((wmi_status != AE_OK) ||
				    ((wmi_status == AE_OK) &&
				     (status == AE_NOT_EXIST)))
					status = wmi_status;
			}
		}
	}

	return status;
}
EXPORT_SYMBOL_GPL(wmi_remove_notify_handler);

/**
 * wmi_get_event_data - Get WMI data associated with an event
 *
 * @event: Event to find
 * @out: Buffer to hold event data. out->pointer should be freed with kfree()
 *
 * Returns extra data associated with an event in WMI.
 */
acpi_status wmi_get_event_data(u32 event, struct acpi_buffer *out)
{
	struct acpi_object_list input;
	union acpi_object params[1];
	struct guid_block *gblock;
	struct wmi_block *wblock;
	struct list_head *p;

	input.count = 1;
	input.pointer = params;
	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = event;

	list_for_each(p, &wmi_block_list) {
		wblock = list_entry(p, struct wmi_block, list);
		gblock = &wblock->gblock;

		if ((gblock->flags & ACPI_WMI_EVENT) &&
			(gblock->notify_id == event))
			return acpi_evaluate_object(wblock->acpi_device->handle,
				"_WED", &input, out);
	}

	return AE_NOT_FOUND;
}
EXPORT_SYMBOL_GPL(wmi_get_event_data);

/**
 * wmi_has_guid - Check if a GUID is available
 * @guid_string: 36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 *
 * Check if a given GUID is defined by _WDG
 */
bool wmi_has_guid(const char *guid_string)
{
	return find_guid(guid_string, NULL);
}
EXPORT_SYMBOL_GPL(wmi_has_guid);

static struct wmi_block *dev_to_wblock(struct device *dev)
{
	return container_of(dev, struct wmi_block, dev.dev);
}

static struct wmi_device *dev_to_wdev(struct device *dev)
{
	return container_of(dev, struct wmi_device, dev);
}

/*
 * sysfs interface
 */
static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sprintf(buf, "wmi:%pUL\n", wblock->gblock.guid);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t guid_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sprintf(buf, "%pUL\n", wblock->gblock.guid);
}
static DEVICE_ATTR_RO(guid);

static ssize_t instance_count_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sprintf(buf, "%d\n", (int)wblock->gblock.instance_count);
}
static DEVICE_ATTR_RO(instance_count);

static ssize_t expensive_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sprintf(buf, "%d\n",
		       (wblock->gblock.flags & ACPI_WMI_EXPENSIVE) != 0);
}
static DEVICE_ATTR_RO(expensive);

static struct attribute *wmi_attrs[] = {
	&dev_attr_modalias.attr,
	&dev_attr_guid.attr,
	&dev_attr_instance_count.attr,
	&dev_attr_expensive.attr,
	NULL,
};
ATTRIBUTE_GROUPS(wmi);

static ssize_t notify_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sprintf(buf, "%02X\n", (unsigned int)wblock->gblock.notify_id);
}
static DEVICE_ATTR_RO(notify_id);

static struct attribute *wmi_event_attrs[] = {
	&dev_attr_notify_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(wmi_event);

static ssize_t object_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	return sprintf(buf, "%c%c\n", wblock->gblock.object_id[0],
		       wblock->gblock.object_id[1]);
}
static DEVICE_ATTR_RO(object_id);

static ssize_t setable_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct wmi_device *wdev = dev_to_wdev(dev);

	return sprintf(buf, "%d\n", (int)wdev->setable);
}
static DEVICE_ATTR_RO(setable);

static struct attribute *wmi_data_attrs[] = {
	&dev_attr_object_id.attr,
	&dev_attr_setable.attr,
	NULL,
};
ATTRIBUTE_GROUPS(wmi_data);

static struct attribute *wmi_method_attrs[] = {
	&dev_attr_object_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(wmi_method);

static int wmi_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	if (add_uevent_var(env, "MODALIAS=wmi:%pUL", wblock->gblock.guid))
		return -ENOMEM;

	if (add_uevent_var(env, "WMI_GUID=%pUL", wblock->gblock.guid))
		return -ENOMEM;

	return 0;
}

static void wmi_dev_release(struct device *dev)
{
	struct wmi_block *wblock = dev_to_wblock(dev);

	kfree(wblock);
}

static int wmi_dev_match(struct device *dev, struct device_driver *driver)
{
	struct wmi_driver *wmi_driver =
		container_of(driver, struct wmi_driver, driver);
	struct wmi_block *wblock = dev_to_wblock(dev);
	const struct wmi_device_id *id = wmi_driver->id_table;

	if (id == NULL)
		return 0;

	while (id->guid_string) {
		uuid_le driver_guid;

		if (WARN_ON(uuid_le_to_bin(id->guid_string, &driver_guid)))
			continue;
		if (!memcmp(&driver_guid, wblock->gblock.guid, 16))
			return 1;

		id++;
	}

	return 0;
}

static int wmi_dev_probe(struct device *dev)
{
	struct wmi_block *wblock = dev_to_wblock(dev);
	struct wmi_driver *wdriver =
		container_of(dev->driver, struct wmi_driver, driver);
	int ret = 0;

	if (ACPI_FAILURE(wmi_method_enable(wblock, 1)))
		dev_warn(dev, "failed to enable device -- probing anyway\n");

	if (wdriver->probe) {
		ret = wdriver->probe(dev_to_wdev(dev));
		if (ret != 0 && ACPI_FAILURE(wmi_method_enable(wblock, 0)))
			dev_warn(dev, "failed to disable device\n");
	}

	return ret;
}

static int wmi_dev_remove(struct device *dev)
{
	struct wmi_block *wblock = dev_to_wblock(dev);
	struct wmi_driver *wdriver =
		container_of(dev->driver, struct wmi_driver, driver);
	int ret = 0;

	if (wdriver->remove)
		ret = wdriver->remove(dev_to_wdev(dev));

	if (ACPI_FAILURE(wmi_method_enable(wblock, 0)))
		dev_warn(dev, "failed to disable device\n");

	return ret;
}

static struct class wmi_bus_class = {
	.name = "wmi_bus",
};

static struct bus_type wmi_bus_type = {
	.name = "wmi",
	.dev_groups = wmi_groups,
	.match = wmi_dev_match,
	.uevent = wmi_dev_uevent,
	.probe = wmi_dev_probe,
	.remove = wmi_dev_remove,
};

static struct device_type wmi_type_event = {
	.name = "event",
	.groups = wmi_event_groups,
	.release = wmi_dev_release,
};

static struct device_type wmi_type_method = {
	.name = "method",
	.groups = wmi_method_groups,
	.release = wmi_dev_release,
};

static struct device_type wmi_type_data = {
	.name = "data",
	.groups = wmi_data_groups,
	.release = wmi_dev_release,
};

static int wmi_create_device(struct device *wmi_bus_dev,
			     const struct guid_block *gblock,
			     struct wmi_block *wblock,
			     struct acpi_device *device)
{
	struct acpi_device_info *info;
	char method[5];
	int result;

	if (gblock->flags & ACPI_WMI_EVENT) {
		wblock->dev.dev.type = &wmi_type_event;
		goto out_init;
	}

	if (gblock->flags & ACPI_WMI_METHOD) {
		wblock->dev.dev.type = &wmi_type_method;
		goto out_init;
	}

	/*
	 * Data Block Query Control Method (WQxx by convention) is
	 * required per the WMI documentation. If it is not present,
	 * we ignore this data block.
	 */
	strcpy(method, "WQ");
	strncat(method, wblock->gblock.object_id, 2);
	result = get_subobj_info(device->handle, method, &info);

	if (result) {
		dev_warn(wmi_bus_dev,
			 "%s data block query control method not found",
			 method);
		return result;
	}

	wblock->dev.dev.type = &wmi_type_data;

	/*
	 * The Microsoft documentation specifically states:
	 *
	 *   Data blocks registered with only a single instance
	 *   can ignore the parameter.
	 *
	 * ACPICA will get mad at us if we call the method with the wrong number
	 * of arguments, so check what our method expects.  (On some Dell
	 * laptops, WQxx may not be a method at all.)
	 */
	if (info->type != ACPI_TYPE_METHOD || info->param_count == 0)
		wblock->read_takes_no_args = true;

	kfree(info);

	strcpy(method, "WS");
	strncat(method, wblock->gblock.object_id, 2);
	result = get_subobj_info(device->handle, method, NULL);

	if (result == 0)
		wblock->dev.setable = true;

 out_init:
	wblock->dev.dev.bus = &wmi_bus_type;
	wblock->dev.dev.parent = wmi_bus_dev;

	dev_set_name(&wblock->dev.dev, "%pUL", gblock->guid);

	device_initialize(&wblock->dev.dev);

	return 0;
}

static void wmi_free_devices(struct acpi_device *device)
{
	struct wmi_block *wblock, *next;

	/* Delete devices for all the GUIDs */
	list_for_each_entry_safe(wblock, next, &wmi_block_list, list) {
		if (wblock->acpi_device == device) {
			list_del(&wblock->list);
			device_unregister(&wblock->dev.dev);
		}
	}
}

static bool guid_already_parsed(struct acpi_device *device,
				const u8 *guid)
{
	struct wmi_block *wblock;

	list_for_each_entry(wblock, &wmi_block_list, list) {
		if (memcmp(wblock->gblock.guid, guid, 16) == 0) {
			/*
			 * Because we historically didn't track the relationship
			 * between GUIDs and ACPI nodes, we don't know whether
			 * we need to suppress GUIDs that are unique on a
			 * given node but duplicated across nodes.
			 */
			dev_warn(&device->dev, "duplicate WMI GUID %pUL (first instance was on %s)\n",
				 guid, dev_name(&wblock->acpi_device->dev));
			return true;
		}
	}

	return false;
}

/*
 * Parse the _WDG method for the GUID data blocks
 */
static int parse_wdg(struct device *wmi_bus_dev, struct acpi_device *device)
{
	struct acpi_buffer out = {ACPI_ALLOCATE_BUFFER, NULL};
	const struct guid_block *gblock;
	struct wmi_block *wblock, *next;
	union acpi_object *obj;
	acpi_status status;
	int retval = 0;
	u32 i, total;

	status = acpi_evaluate_object(device->handle, "_WDG", NULL, &out);
	if (ACPI_FAILURE(status))
		return -ENXIO;

	obj = (union acpi_object *) out.pointer;
	if (!obj)
		return -ENXIO;

	if (obj->type != ACPI_TYPE_BUFFER) {
		retval = -ENXIO;
		goto out_free_pointer;
	}

	gblock = (const struct guid_block *)obj->buffer.pointer;
	total = obj->buffer.length / sizeof(struct guid_block);

	for (i = 0; i < total; i++) {
		if (debug_dump_wdg)
			wmi_dump_wdg(&gblock[i]);

		/*
		 * Some WMI devices, like those for nVidia hooks, have a
		 * duplicate GUID. It's not clear what we should do in this
		 * case yet, so for now, we'll just ignore the duplicate
		 * for device creation.
		 */
		if (guid_already_parsed(device, gblock[i].guid))
			continue;

		wblock = kzalloc(sizeof(struct wmi_block), GFP_KERNEL);
		if (!wblock) {
			retval = -ENOMEM;
			break;
		}

		wblock->acpi_device = device;
		wblock->gblock = gblock[i];

		retval = wmi_create_device(wmi_bus_dev, &gblock[i], wblock, device);
		if (retval) {
			kfree(wblock);
			continue;
		}

		list_add_tail(&wblock->list, &wmi_block_list);

		if (debug_event) {
			wblock->handler = wmi_notify_debug;
			wmi_method_enable(wblock, 1);
		}
	}

	/*
	 * Now that all of the devices are created, add them to the
	 * device tree and probe subdrivers.
	 */
	list_for_each_entry_safe(wblock, next, &wmi_block_list, list) {
		if (wblock->acpi_device != device)
			continue;

		retval = device_add(&wblock->dev.dev);
		if (retval) {
			dev_err(wmi_bus_dev, "failed to register %pULL\n",
				wblock->gblock.guid);
			if (debug_event)
				wmi_method_enable(wblock, 0);
			list_del(&wblock->list);
			put_device(&wblock->dev.dev);
		}
	}

out_free_pointer:
	kfree(out.pointer);
	return retval;
}

/*
 * WMI can have EmbeddedControl access regions. In which case, we just want to
 * hand these off to the EC driver.
 */
static acpi_status
acpi_wmi_ec_space_handler(u32 function, acpi_physical_address address,
		      u32 bits, u64 *value,
		      void *handler_context, void *region_context)
{
	int result = 0, i = 0;
	u8 temp = 0;

	if ((address > 0xFF) || !value)
		return AE_BAD_PARAMETER;

	if (function != ACPI_READ && function != ACPI_WRITE)
		return AE_BAD_PARAMETER;

	if (bits != 8)
		return AE_BAD_PARAMETER;

	if (function == ACPI_READ) {
		result = ec_read(address, &temp);
		(*value) |= ((u64)temp) << i;
	} else {
		temp = 0xff & ((*value) >> i);
		result = ec_write(address, temp);
	}

	switch (result) {
	case -EINVAL:
		return AE_BAD_PARAMETER;
		break;
	case -ENODEV:
		return AE_NOT_FOUND;
		break;
	case -ETIME:
		return AE_TIME;
		break;
	default:
		return AE_OK;
	}
}

static void acpi_wmi_notify_handler(acpi_handle handle, u32 event,
				    void *context)
{
	struct guid_block *block;
	struct wmi_block *wblock;
	struct list_head *p;
	bool found_it = false;

	list_for_each(p, &wmi_block_list) {
		wblock = list_entry(p, struct wmi_block, list);
		block = &wblock->gblock;

		if (wblock->acpi_device->handle == handle &&
		    (block->flags & ACPI_WMI_EVENT) &&
		    (block->notify_id == event))
		{
			found_it = true;
			break;
		}
	}

	if (!found_it)
		return;

	/* If a driver is bound, then notify the driver. */
	if (wblock->dev.dev.driver) {
		struct wmi_driver *driver;
		struct acpi_object_list input;
		union acpi_object params[1];
		struct acpi_buffer evdata = { ACPI_ALLOCATE_BUFFER, NULL };
		acpi_status status;

		driver = container_of(wblock->dev.dev.driver,
				      struct wmi_driver, driver);

		input.count = 1;
		input.pointer = params;
		params[0].type = ACPI_TYPE_INTEGER;
		params[0].integer.value = event;

		status = acpi_evaluate_object(wblock->acpi_device->handle,
					      "_WED", &input, &evdata);
		if (ACPI_FAILURE(status)) {
			dev_warn(&wblock->dev.dev,
				 "failed to get event data\n");
			return;
		}

		if (driver->notify)
			driver->notify(&wblock->dev,
				       (union acpi_object *)evdata.pointer);

		kfree(evdata.pointer);
	} else if (wblock->handler) {
		/* Legacy handler */
		wblock->handler(event, wblock->handler_data);
	}

	if (debug_event) {
		pr_debug("DEBUG Event GUID: %pUL\n",
			wblock->gblock.guid);
	}

	acpi_bus_generate_netlink_event(
		wblock->acpi_device->pnp.device_class,
		dev_name(&wblock->dev.dev),
		event, 0);

}

static int acpi_wmi_remove(struct platform_device *device)
{
	struct acpi_device *acpi_device = ACPI_COMPANION(&device->dev);

	acpi_remove_notify_handler(acpi_device->handle, ACPI_DEVICE_NOTIFY,
				   acpi_wmi_notify_handler);
	acpi_remove_address_space_handler(acpi_device->handle,
				ACPI_ADR_SPACE_EC, &acpi_wmi_ec_space_handler);
	wmi_free_devices(acpi_device);
	device_unregister((struct device *)dev_get_drvdata(&device->dev));

	return 0;
}

static int acpi_wmi_probe(struct platform_device *device)
{
	struct acpi_device *acpi_device;
	struct device *wmi_bus_dev;
	acpi_status status;
	int error;

	acpi_device = ACPI_COMPANION(&device->dev);
	if (!acpi_device) {
		dev_err(&device->dev, "ACPI companion is missing\n");
		return -ENODEV;
	}

	status = acpi_install_address_space_handler(acpi_device->handle,
						    ACPI_ADR_SPACE_EC,
						    &acpi_wmi_ec_space_handler,
						    NULL, NULL);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "Error installing EC region handler\n");
		return -ENODEV;
	}

	status = acpi_install_notify_handler(acpi_device->handle,
					     ACPI_DEVICE_NOTIFY,
					     acpi_wmi_notify_handler,
					     NULL);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "Error installing notify handler\n");
		error = -ENODEV;
		goto err_remove_ec_handler;
	}

	wmi_bus_dev = device_create(&wmi_bus_class, &device->dev, MKDEV(0, 0),
				    NULL, "wmi_bus-%s", dev_name(&device->dev));
	if (IS_ERR(wmi_bus_dev)) {
		error = PTR_ERR(wmi_bus_dev);
		goto err_remove_notify_handler;
	}
	dev_set_drvdata(&device->dev, wmi_bus_dev);

	error = parse_wdg(wmi_bus_dev, acpi_device);
	if (error) {
		pr_err("Failed to parse WDG method\n");
		goto err_remove_busdev;
	}

	return 0;

err_remove_busdev:
	device_unregister(wmi_bus_dev);

err_remove_notify_handler:
	acpi_remove_notify_handler(acpi_device->handle, ACPI_DEVICE_NOTIFY,
				   acpi_wmi_notify_handler);

err_remove_ec_handler:
	acpi_remove_address_space_handler(acpi_device->handle,
					  ACPI_ADR_SPACE_EC,
					  &acpi_wmi_ec_space_handler);

	return error;
}

int __must_check __wmi_driver_register(struct wmi_driver *driver,
				       struct module *owner)
{
	driver->driver.owner = owner;
	driver->driver.bus = &wmi_bus_type;

	return driver_register(&driver->driver);
}
EXPORT_SYMBOL(__wmi_driver_register);

void wmi_driver_unregister(struct wmi_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL(wmi_driver_unregister);

static int __init acpi_wmi_init(void)
{
	int error;

	if (acpi_disabled)
		return -ENODEV;

	error = class_register(&wmi_bus_class);
	if (error)
		return error;

	error = bus_register(&wmi_bus_type);
	if (error)
		goto err_unreg_class;

	error = platform_driver_register(&acpi_wmi_driver);
	if (error) {
		pr_err("Error loading mapper\n");
		goto err_unreg_bus;
	}

	return 0;

err_unreg_bus:
	bus_unregister(&wmi_bus_type);

err_unreg_class:
	class_unregister(&wmi_bus_class);

	return error;
}

static void __exit acpi_wmi_exit(void)
{
	platform_driver_unregister(&acpi_wmi_driver);
	class_unregister(&wmi_bus_class);
	bus_unregister(&wmi_bus_type);
}

subsys_initcall_sync(acpi_wmi_init);
module_exit(acpi_wmi_exit);
