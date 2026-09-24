#include "include/flutter_usb_event/flutter_usb_event_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <libudev.h>
#include <sys/utsname.h>

#include <cstring>

#define FLUTTER_USB_EVENT_PLUGIN(obj)                                     \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), flutter_usb_event_plugin_get_type(), \
                              FlutterUsbEventPlugin))

#define METHOD_CHANNEL_NAME "flutter_usb_event"

// Serial devices (e.g. /dev/ttyACM*, /dev/ttyUSB*), which is what USB serial
// hardware appears as. Filtering on "usb" instead would fire before the
// device node exists.
#define UDEV_SUBSYSTEM "tty"

struct _FlutterUsbEventPlugin {
  GObject parent_instance;

  FlMethodChannel* channel;

  struct udev* udev;
  struct udev_monitor* monitor;
  guint monitor_source_id;
};

G_DEFINE_TYPE(FlutterUsbEventPlugin, flutter_usb_event_plugin, g_object_get_type())

// Returns the USB product name if udev knows it, falling back to the device node
static const gchar* get_device_name(struct udev_device* device) {
  const gchar* name = udev_device_get_property_value(device, "ID_MODEL");
  if (name == nullptr) name = udev_device_get_devnode(device);
  return name != nullptr ? name : "Unknown device";
}

static void stop_listening(FlutterUsbEventPlugin* self) {
  if (self->monitor_source_id != 0) {
    g_source_remove(self->monitor_source_id);
    self->monitor_source_id = 0;
  }
  if (self->monitor != nullptr) {
    udev_monitor_unref(self->monitor);
    self->monitor = nullptr;
  }
  if (self->udev != nullptr) {
    udev_unref(self->udev);
    self->udev = nullptr;
  }
}

static gboolean on_udev_event(gint fd, GIOCondition condition, gpointer user_data) {
  FlutterUsbEventPlugin* self = FLUTTER_USB_EVENT_PLUGIN(user_data);

  if (condition & (G_IO_ERR | G_IO_HUP)) {
    // Returning G_SOURCE_REMOVE removes the source, so don't remove it again
    self->monitor_source_id = 0;
    stop_listening(self);
    return G_SOURCE_REMOVE;
  }

  struct udev_device* device = udev_monitor_receive_device(self->monitor);
  if (device == nullptr) return G_SOURCE_CONTINUE;

  const gchar* action = udev_device_get_action(device);
  const gchar* method = nullptr;
  if (g_strcmp0(action, "add") == 0) {
    method = "onDeviceConnected";
  } else if (g_strcmp0(action, "remove") == 0) {
    method = "onDeviceDisconnected";
  }

  if (method != nullptr) {
    g_autoptr(FlValue) args = fl_value_new_string(get_device_name(device));
    fl_method_channel_invoke_method(self->channel, method, args, nullptr, nullptr, nullptr);
  }

  udev_device_unref(device);
  return G_SOURCE_CONTINUE;
}

static gboolean start_listening(FlutterUsbEventPlugin* self) {
  if (self->monitor != nullptr) return TRUE;

  self->udev = udev_new();
  if (self->udev == nullptr) return FALSE;

  // "udev" rather than "kernel" so events arrive after udev rules have
  // created the device node and applied its permissions
  self->monitor = udev_monitor_new_from_netlink(self->udev, "udev");
  if (self->monitor == nullptr
      || udev_monitor_filter_add_match_subsystem_devtype(self->monitor, UDEV_SUBSYSTEM, nullptr) < 0
      || udev_monitor_enable_receiving(self->monitor) < 0) {
    stop_listening(self);
    return FALSE;
  }

  self->monitor_source_id = g_unix_fd_add(
      udev_monitor_get_fd(self->monitor),
      static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP),
      on_udev_event, self);

  return TRUE;
}

static void flutter_usb_event_plugin_handle_method_call(FlutterUsbEventPlugin* self,
                                                        FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;

  const gchar* method = fl_method_call_get_name(method_call);

  if (strcmp(method, "startListening") == 0) {
    if (start_listening(self)) {
      g_autoptr(FlValue) result = fl_value_new_string("Listening started");
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(result));
    } else {
      response = FL_METHOD_RESPONSE(fl_method_error_response_new(
          "UDEV_ERROR", "Failed to create udev monitor", nullptr));
    }
  } else if (strcmp(method, "stopListening") == 0) {
    stop_listening(self);
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  } else if (strcmp(method, "getPlatformVersion") == 0) {
    struct utsname uname_data = {};
    uname(&uname_data);
    g_autofree gchar* version = g_strdup_printf("Linux %s", uname_data.version);
    g_autoptr(FlValue) result = fl_value_new_string(version);
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(result));
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  fl_method_call_respond(method_call, response, nullptr);
}

static void flutter_usb_event_plugin_dispose(GObject* object) {
  FlutterUsbEventPlugin* self = FLUTTER_USB_EVENT_PLUGIN(object);

  stop_listening(self);
  g_clear_object(&self->channel);

  G_OBJECT_CLASS(flutter_usb_event_plugin_parent_class)->dispose(object);
}

static void flutter_usb_event_plugin_class_init(FlutterUsbEventPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = flutter_usb_event_plugin_dispose;
}

static void flutter_usb_event_plugin_init(FlutterUsbEventPlugin* self) {}

static void method_call_cb(FlMethodChannel* channel, FlMethodCall* method_call,
                           gpointer user_data) {
  FlutterUsbEventPlugin* plugin = FLUTTER_USB_EVENT_PLUGIN(user_data);
  flutter_usb_event_plugin_handle_method_call(plugin, method_call);
}

void flutter_usb_event_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  FlutterUsbEventPlugin* plugin = FLUTTER_USB_EVENT_PLUGIN(
      g_object_new(flutter_usb_event_plugin_get_type(), nullptr));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  plugin->channel = fl_method_channel_new(fl_plugin_registrar_get_messenger(registrar),
                                          METHOD_CHANNEL_NAME, FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(plugin->channel, method_call_cb,
                                            g_object_ref(plugin), g_object_unref);

  g_object_unref(plugin);
}
