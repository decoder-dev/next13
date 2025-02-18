#define pr_fmt(fmt)	"[MI_TOUCH][%s:%d]" fmt, __func__, __LINE__

#include "xiaomi_touch.h"

static struct xiaomi_touch_pdata *touch_pdata;
int mi_log_level;

static int xiaomi_touch_dev_open(struct inode *inode, struct file *file)
{
	struct xiaomi_touch *dev = NULL;
	int i = MINOR(inode->i_rdev);
	struct xiaomi_touch_pdata *touch_pdata;

	pr_debug("%s\n", __func__);
	dev = xiaomi_touch_dev_get(i);
	if (!dev) {
		MI_TOUCH_LOGE(1, "can't get dev!");
		return -ENOMEM;
	}
	touch_pdata = dev_get_drvdata(dev->dev);

	file->private_data = touch_pdata;
	return 0;
}

static ssize_t xiaomi_touch_dev_read(struct file *file, char __user *buf,
			   size_t count, loff_t *pos)
{
	return 0;
}

static ssize_t xiaomi_touch_dev_write(struct file *file,
		const char __user *buf, size_t count, loff_t *pos)
{
	return 0;
}

static unsigned int xiaomi_touch_dev_poll(struct file *file,
		poll_table *wait)
{
	return 0;
}

static long xiaomi_touch_dev_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int ret = -EINVAL;
	int buf[VALUE_TYPE_SIZE] = {0,};
	struct xiaomi_touch_pdata *pdata = file->private_data;
	void __user *argp = (void __user *) arg;
	struct xiaomi_touch_interface *touch_data = pdata->touch_data;
	struct xiaomi_touch *dev = pdata->device;
	int user_cmd = _IOC_NR(cmd);

	if (!pdata || !touch_data || !dev) {
		MI_TOUCH_LOGE(1, "invalid memory!");
		return -ENOMEM;
	}

	mutex_lock(&dev->mutex);
	ret = copy_from_user(&buf, (int __user *)argp, sizeof(buf));

	MI_TOUCH_LOGI(1, "cmd:%d, mode:%d, value:%d!", user_cmd, buf[0], buf[1]);

	switch (user_cmd) {
	case SET_CUR_VALUE:
		if (touch_data->setModeValue) {
			if (buf[0] == Touch_Debug_Level && mi_log_level < TOUCH_DEBUG)
				mi_log_level = buf[1] != 0 ? TOUCH_NOTICE : TOUCH_INFO;
			else
				buf[0] = touch_data->setModeValue(buf[0], buf[1]);
		}
		break;
	case GET_CUR_VALUE:
	case GET_DEF_VALUE:
	case GET_MIN_VALUE:
	case GET_MAX_VALUE:
		if (touch_data->getModeValue)
			buf[0] = touch_data->getModeValue(buf[0], user_cmd);
		break;
	case RESET_MODE:
		if (touch_data->resetMode)
			buf[0] = touch_data->resetMode(buf[0]);
		break;
	case GET_MODE_VALUE:
		if (touch_data->getModeValue)
			ret = touch_data->getModeAll(buf[0], buf);
		break;
	default:
		MI_TOUCH_LOGE(1, "don't support mode!");
		ret = -EINVAL;
		break;
	}

	if (ret >= 0)
		ret = copy_to_user((int __user *)argp, &buf, sizeof(buf));
	else
		MI_TOUCH_LOGE(1, "can't get data from touch driver!");

	mutex_unlock(&dev->mutex);

	return ret;
}

static int xiaomi_touch_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations xiaomitouch_dev_fops = {
	.owner = THIS_MODULE,
	.open = xiaomi_touch_dev_open,
	.read = xiaomi_touch_dev_read,
	.write = xiaomi_touch_dev_write,
	.poll = xiaomi_touch_dev_poll,
	.unlocked_ioctl = xiaomi_touch_dev_ioctl,
	.compat_ioctl = xiaomi_touch_dev_ioctl,
	.release = xiaomi_touch_dev_release,
	.llseek	= no_llseek,
};

static struct xiaomi_touch xiaomi_touch_dev = {
	.misc_dev = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "xiaomi-touch",
		.fops = &xiaomitouch_dev_fops,
		.parent = NULL,
	},
	.mutex = __MUTEX_INITIALIZER(xiaomi_touch_dev.mutex),
	.palm_mutex = __MUTEX_INITIALIZER(xiaomi_touch_dev.palm_mutex),
	.psensor_mutex = __MUTEX_INITIALIZER(xiaomi_touch_dev.psensor_mutex),
	.wait_queue = __WAIT_QUEUE_HEAD_INITIALIZER(xiaomi_touch_dev.wait_queue),
};

struct xiaomi_touch *xiaomi_touch_dev_get(int minor)
{
	if (xiaomi_touch_dev.misc_dev.minor == minor)
		return &xiaomi_touch_dev;
	else
		return NULL;
}

struct class *get_xiaomi_touch_class(void)
{
	return xiaomi_touch_dev.class;
}

struct device *get_xiaomi_touch_dev(void)
{
	return xiaomi_touch_dev.dev;
}

int xiaomitouch_register_modedata(struct xiaomi_touch_interface *data)
{
	int ret = 0;
	struct xiaomi_touch_interface *touch_data = NULL;

	if (!touch_pdata)
		ret = -ENOMEM;

	touch_data = touch_pdata->touch_data;
	MI_TOUCH_LOGI(1, "Enter");

	mutex_lock(&xiaomi_touch_dev.mutex);

	touch_data->setModeValue = data->setModeValue;
	touch_data->getModeValue = data->getModeValue;
	touch_data->resetMode= data->resetMode;
	touch_data->getModeAll = data->getModeAll;
	touch_data->palm_sensor_read = data->palm_sensor_read;
	touch_data->palm_sensor_write = data->palm_sensor_write;
	touch_data->p_sensor_read = data->p_sensor_read;
	touch_data->p_sensor_write = data->p_sensor_write;
	touch_data->touch_vendor_read = data->touch_vendor_read;
	touch_data->panel_color_read = data->panel_color_read;
	touch_data->panel_display_read = data->panel_display_read;
	touch_data->panel_vendor_read = data->panel_vendor_read;

	mutex_unlock(&xiaomi_touch_dev.mutex);

	return ret;
}

int update_palm_sensor_value(int value)
{
	struct xiaomi_touch *dev = NULL;

	mutex_lock(&xiaomi_touch_dev.palm_mutex);

	if (!touch_pdata) {
		mutex_unlock(&xiaomi_touch_dev.palm_mutex);
		return -ENODEV;
	}

	dev = touch_pdata->device;

	if (value != touch_pdata->palm_value) {
		MI_TOUCH_LOGI(1, "value:%d", value);
		touch_pdata->palm_value = value;
		touch_pdata->palm_changed = true;
		wake_up(&dev->wait_queue);
	}

	mutex_unlock(&xiaomi_touch_dev.palm_mutex);
	return 0;
}

static ssize_t palm_sensor_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
	struct xiaomi_touch_pdata *pdata = dev_get_drvdata(dev);
	struct xiaomi_touch *touch_dev = pdata->device;

	wait_event_interruptible(touch_dev->wait_queue, pdata->palm_changed);
	pdata->palm_changed = false;

	return snprintf(buf, PAGE_SIZE, "%d\n", pdata->palm_value);
}

static ssize_t palm_sensor_store(struct device *dev,
struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct xiaomi_touch_pdata *pdata = dev_get_drvdata(dev);

	if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;

	if(input == 3) {
		touch_pdata->palm_value = 3;
		touch_pdata->palm_changed = true;
		wake_up(&touch_pdata->device->wait_queue);
		MI_TOUCH_LOGE(1, "palm_sensor enter test mode");
	} else {
		if (pdata->touch_data->palm_sensor_write)
			pdata->touch_data->palm_sensor_write(!!input);
		else
			MI_TOUCH_LOGE(1, "has not implement!");

		MI_TOUCH_LOGI(1, "value:%d", !!input);
	}

	return count;
}

int update_p_sensor_value(int value)
{
	struct xiaomi_touch *dev = NULL;

	mutex_lock(&xiaomi_touch_dev.psensor_mutex);

	if (!touch_pdata) {
		mutex_unlock(&xiaomi_touch_dev.psensor_mutex);
		return -ENODEV;
	}

	dev = touch_pdata->device;

	if (value != touch_pdata->psensor_value) {
		pr_debug("%s value:%d\n", __func__, value);
		touch_pdata->psensor_value = value;
		touch_pdata->psensor_changed = true;
		wake_up(&dev->wait_queue);
	}

	mutex_unlock(&xiaomi_touch_dev.psensor_mutex);
	return 0;
}

static ssize_t p_sensor_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
	struct xiaomi_touch_pdata *pdata = dev_get_drvdata(dev);
	struct xiaomi_touch *touch_dev = pdata->device;

	wait_event_interruptible(touch_dev->wait_queue, pdata->psensor_changed);
	pdata->psensor_changed = false;

	return snprintf(buf, PAGE_SIZE, "%d\n", pdata->psensor_value);
}

static ssize_t p_sensor_store(struct device *dev,
struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct xiaomi_touch_pdata *pdata = dev_get_drvdata(dev);

	if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;

	if (pdata->touch_data->p_sensor_write)
		pdata->touch_data->p_sensor_write(!!input);
	else
		MI_TOUCH_LOGE(1, "has not implement!");

	MI_TOUCH_LOGI(1, "value:%d", !!input);

	return count;
}

static ssize_t xiaomi_touch_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char value = '0';
	struct xiaomi_touch_pdata *pdata = dev_get_drvdata(dev);
	if (pdata->touch_data->touch_vendor_read)
		value = pdata->touch_data->touch_vendor_read();
	else
		MI_TOUCH_LOGE(1, "Touch vendor not define,return default value!");
	return snprintf(buf, PAGE_SIZE, "%c\n", value);
}

static ssize_t xiaomi_panel_color_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char value = '0';
	struct xiaomi_touch_pdata *pdata = dev_get_drvdata(dev);
	if (pdata->touch_data->panel_color_read)
		value = pdata->touch_data->panel_color_read();
	else
		MI_TOUCH_LOGE(1, "Touch vendor not define,return default value!");
	if (value == '0')
		return 0;
	return snprintf(buf, PAGE_SIZE, "%c\n", value);
}

static ssize_t xiaomi_panel_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char value = '0';
	struct xiaomi_touch_pdata *pdata = dev_get_drvdata(dev);
	if (pdata->touch_data->panel_vendor_read)
		value = pdata->touch_data->panel_vendor_read();
	else
		MI_TOUCH_LOGE(1, "Touch vendor not define,return default value!");
	if (value == '0')
		return 0;
	return snprintf(buf, PAGE_SIZE, "%c\n", value);
}

static ssize_t xiaomi_panel_display_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char value = '0';
	struct xiaomi_touch_pdata *pdata = dev_get_drvdata(dev);
	if (pdata->touch_data->panel_display_read)
		value = pdata->touch_data->panel_display_read();
	else
		MI_TOUCH_LOGE(1, "Touch vendor not define,return default value!");
	if (value == '0')
		return 0;
	return snprintf(buf, PAGE_SIZE, "%c\n", value);
}

static ssize_t log_debug_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", mi_log_level);
}

static ssize_t log_debug_store(struct device *dev,
struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	if (sscanf(buf, "%d", &input) < 0)
		return -EINVAL;

	mi_log_level = input == 0 ? TOUCH_INFO : TOUCH_DEBUG;

	MI_TOUCH_LOGI(1, "log_level: %d", mi_log_level);
	return count;
}


static DEVICE_ATTR(palm_sensor, (S_IRUGO | S_IWUSR | S_IWGRP),
		   palm_sensor_show, palm_sensor_store);

static DEVICE_ATTR(p_sensor, (S_IRUGO | S_IWUSR | S_IWGRP),
		   p_sensor_show, p_sensor_store);

static DEVICE_ATTR(touch_vendor, 0644, xiaomi_touch_vendor_show, NULL);
static DEVICE_ATTR(panel_display, 0644, xiaomi_panel_display_show, NULL);
static DEVICE_ATTR(panel_vendor, 0644, xiaomi_panel_vendor_show, NULL);
static DEVICE_ATTR(panel_color, 0644, xiaomi_panel_color_show, NULL);

static DEVICE_ATTR(log_debug, (S_IRUGO | S_IWUSR | S_IWGRP),
		log_debug_show, log_debug_store);

static struct attribute *touch_attr_group[] = {
	&dev_attr_palm_sensor.attr,
	&dev_attr_p_sensor.attr,
	&dev_attr_touch_vendor.attr,
	&dev_attr_panel_vendor.attr,
	&dev_attr_panel_display.attr,
	&dev_attr_panel_color.attr,
	&dev_attr_log_debug.attr,
	NULL,
};

static const struct of_device_id xiaomi_touch_of_match[] = {
	{ .compatible = "xiaomi-touch", },
	{ },
};

static int xiaomi_touch_parse_dt(struct device *dev, struct xiaomi_touch_pdata *data)
{
	int ret;
	struct device_node *np;

	np = dev->of_node;
	if (!np)
		return -ENODEV;

	ret = of_property_read_string(np, "touch,name", &data->name);
	if (ret)
		return ret;

	MI_TOUCH_LOGI(1, "touch,name:%s", data->name);

	return 0;
}

static int xiaomi_touch_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct xiaomi_touch_pdata *pdata;
	mi_log_level = TOUCH_INFO;
	pdata = devm_kzalloc(dev, sizeof(struct xiaomi_touch_pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	MI_TOUCH_LOGI(1, "Enter");

	ret = xiaomi_touch_parse_dt(dev, pdata);
	if (ret < 0) {
		MI_TOUCH_LOGE(1, "parse dt error:%d", ret);
		goto parse_dt_err;
	}

	ret = misc_register(&xiaomi_touch_dev.misc_dev);
	if (ret) {
		MI_TOUCH_LOGE(1, "create misc device err:%d", ret);
		goto parse_dt_err;
	}

	if (!xiaomi_touch_dev.class)
		xiaomi_touch_dev.class = class_create(THIS_MODULE, "touch");

	if (!xiaomi_touch_dev.class) {
		MI_TOUCH_LOGE(1, "create device class err");
		goto class_create_err;
	}

	xiaomi_touch_dev.dev = device_create(xiaomi_touch_dev.class, NULL, 'T', NULL, "touch_dev");
	if (!xiaomi_touch_dev.dev) {
		MI_TOUCH_LOGE(1, "create device dev err");
		goto device_create_err;
	}

	pdata->touch_data = (struct xiaomi_touch_interface *)kzalloc(sizeof(struct xiaomi_touch_interface), GFP_KERNEL);
	if (pdata->touch_data == NULL) {
		ret = -ENOMEM;
		MI_TOUCH_LOGE(1, "alloc mem for touch_data");
		goto data_mem_err;
	}

	pdata->device = &xiaomi_touch_dev;
	dev_set_drvdata(xiaomi_touch_dev.dev, pdata);

	touch_pdata = pdata;

	xiaomi_touch_dev.attrs.attrs = touch_attr_group;
	ret = sysfs_create_group(&xiaomi_touch_dev.dev->kobj, &xiaomi_touch_dev.attrs);
	if (ret) {
		MI_TOUCH_LOGE(1, "ERROR: Cannot create sysfs structure!:%d", ret);
		ret = -ENODEV;
		goto sys_group_err;
	}

	MI_TOUCH_LOGI(1, "Over");

	return ret;

sys_group_err:
	if (pdata->touch_data) {
		kfree(pdata->touch_data);
		pdata->touch_data = NULL;
	}
data_mem_err:
	device_destroy(xiaomi_touch_dev.class, 0);
device_create_err:
	class_destroy(xiaomi_touch_dev.class);
class_create_err:
	misc_deregister(&xiaomi_touch_dev.misc_dev);
parse_dt_err:
	MI_TOUCH_LOGE(1, "Fail!");
	return ret;
}

static int xiaomi_touch_remove(struct platform_device *pdev)
{
	device_destroy(xiaomi_touch_dev.class, 0);
	class_destroy(xiaomi_touch_dev.class);
	misc_deregister(&xiaomi_touch_dev.misc_dev);
	if (touch_pdata->touch_data) {
		kfree(touch_pdata->touch_data);
		touch_pdata->touch_data = NULL;
	}

	return 0;
}

static struct platform_driver xiaomi_touch_device_driver = {
	.probe		= xiaomi_touch_probe,
	.remove		= xiaomi_touch_remove,
	.driver		= {
		.name	= "xiaomi-touch",
		.of_match_table = of_match_ptr(xiaomi_touch_of_match),
	}
};

static int __init xiaomi_touch_init(void)
{
	int ret;
	MI_TOUCH_LOGI(1, "enter ++");
	ret = platform_driver_register(&xiaomi_touch_device_driver);
	MI_TOUCH_LOGI(1, "out --");
	return ret;
}

static void __exit xiaomi_touch_exit(void)
{
	platform_driver_unregister(&xiaomi_touch_device_driver);
}

subsys_initcall(xiaomi_touch_init);
module_exit(xiaomi_touch_exit);
