#include "include/tray_manager/tray_manager_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include <cstring>
#include <string>
#include <vector>

#define TRAY_MANAGER_PLUGIN(obj)                                     \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), tray_manager_plugin_get_type(), \
                              TrayManagerPlugin))

TrayManagerPlugin* plugin_instance = nullptr;

GtkWidget* menu = nullptr;
GDBusConnection* sni_connection = nullptr;
GDBusNodeInfo* sni_node_info = nullptr;
guint sni_registration_id = 0;
GDBusNodeInfo* dbusmenu_node_info = nullptr;
guint dbusmenu_registration_id = 0;
guint watcher_watch_id = 0;
gboolean watcher_registered = FALSE;
GdkPixbuf* tray_icon_pixbuf = nullptr;
gchar* tray_title = nullptr;
gboolean tray_visible = FALSE;

struct _TrayManagerPlugin {
  GObject parent_instance;
  FlPluginRegistrar* registrar;
  FlMethodChannel* channel;
};

G_DEFINE_TYPE(TrayManagerPlugin, tray_manager_plugin, g_object_get_type())

static const char kDbusMenuIntrospectionXml[] =
    "<node>"
    " <interface name='com.canonical.dbusmenu'>"
    "  <property name='Version' type='u' access='read'/>"
    "  <property name='TextDirection' type='s' access='read'/>"
    "  <property name='Status' type='s' access='read'/>"
    "  <property name='IconThemePath' type='as' access='read'/>"
    "  <method name='GetLayout'>"
    "   <arg type='i' direction='in'/><arg type='i' direction='in'/>"
    "   <arg type='as' direction='in'/><arg type='u' direction='out'/>"
    "   <arg type='(ia{sv}av)' direction='out'/>"
    "  </method>"
    "  <method name='GetGroupProperties'>"
    "   <arg type='ai' direction='in'/><arg type='as' direction='in'/>"
    "   <arg type='a(ia{sv})' direction='out'/>"
    "  </method>"
    "  <method name='GetProperty'>"
    "   <arg type='i' direction='in'/><arg type='s' direction='in'/>"
    "   <arg type='v' direction='out'/>"
    "  </method>"
    "  <method name='Event'>"
    "   <arg type='i' direction='in'/><arg type='s' direction='in'/>"
    "   <arg type='v' direction='in'/><arg type='u' direction='in'/>"
    "  </method>"
    "  <method name='EventGroup'>"
    "   <arg type='a(isvu)' direction='in'/><arg type='a(ias)' direction='out'/>"
    "  </method>"
    "  <method name='AboutToShow'>"
    "   <arg type='i' direction='in'/><arg type='b' direction='out'/>"
    "  </method>"
    "  <method name='AboutToShowGroup'>"
    "   <arg type='ai' direction='in'/><arg type='ai' direction='out'/>"
    "   <arg type='ai' direction='out'/>"
    "  </method>"
    "  <signal name='LayoutUpdated'><arg type='u'/><arg type='i'/></signal>"
    "  <signal name='ItemsPropertiesUpdated'>"
    "   <arg type='a(ia{sv})'/><arg type='a(ias)'/>"
    "  </signal>"
    " </interface>"
    "</node>";

static const char kSniIntrospectionXml[] =
    "<node>"
    " <interface name='org.kde.StatusNotifierItem'>"
    "  <property name='Category' type='s' access='read'/>"
    "  <property name='Id' type='s' access='read'/>"
    "  <property name='Title' type='s' access='read'/>"
    "  <property name='Status' type='s' access='read'/>"
    "  <property name='WindowId' type='i' access='read'/>"
    "  <property name='IconName' type='s' access='read'/>"
    "  <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "  <property name='OverlayIconName' type='s' access='read'/>"
    "  <property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
    "  <property name='AttentionIconName' type='s' access='read'/>"
    "  <property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
    "  <property name='AttentionMovieName' type='s' access='read'/>"
    "  <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "  <property name='ItemIsMenu' type='b' access='read'/>"
    "  <property name='Menu' type='o' access='read'/>"
    "  <property name='XAyatanaLabel' type='s' access='read'/>"
    "  <property name='XAyatanaLabelGuide' type='s' access='read'/>"
    "  <method name='ContextMenu'><arg type='i' direction='in'/>"
    "   <arg type='i' direction='in'/></method>"
    "  <method name='Activate'><arg type='i' direction='in'/>"
    "   <arg type='i' direction='in'/></method>"
    "  <method name='SecondaryActivate'><arg type='i' direction='in'/>"
    "   <arg type='i' direction='in'/></method>"
    "  <method name='Scroll'><arg type='i' direction='in'/>"
    "   <arg type='s' direction='in'/></method>"
    "  <signal name='NewTitle'/>"
    "  <signal name='NewIcon'/>"
    "  <signal name='NewToolTip'/>"
    "  <signal name='NewStatus'><arg type='s'/></signal>"
    " </interface>"
    "</node>";

static GVariant* build_icon_pixmaps(GdkPixbuf* pixbuf) {
  GVariantBuilder array_builder;
  g_variant_builder_init(&array_builder, G_VARIANT_TYPE("a(iiay)"));

  if (pixbuf != nullptr) {
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    std::vector<guint8> argb;
    argb.reserve(static_cast<size_t>(width) * height * 4);

    for (int y = 0; y < height; ++y) {
      const guchar* pixel = pixels + y * rowstride;
      for (int x = 0; x < width; ++x) {
        argb.push_back(has_alpha ? pixel[3] : 255);
        argb.push_back(pixel[0]);
        argb.push_back(pixel[1]);
        argb.push_back(pixel[2]);
        pixel += channels;
      }
    }

    GVariantBuilder entry_builder;
    g_variant_builder_init(&entry_builder, G_VARIANT_TYPE("(iiay)"));
    g_variant_builder_add(&entry_builder, "i", width);
    g_variant_builder_add(&entry_builder, "i", height);
    g_variant_builder_add_value(
        &entry_builder,
        g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, argb.data(),
                                  argb.size(), sizeof(guint8)));
    g_variant_builder_add_value(&array_builder,
                                g_variant_builder_end(&entry_builder));
  }

  return g_variant_builder_end(&array_builder);
}

static void emit_sni_signal(const gchar* signal_name,
                            GVariant* parameters = nullptr) {
  if (sni_connection == nullptr) return;
  g_dbus_connection_emit_signal(
      sni_connection, nullptr, "/StatusNotifierItem",
      "org.kde.StatusNotifierItem", signal_name, parameters, nullptr);
}

static void emit_tray_event(const gchar* event_name) {
  if (plugin_instance == nullptr || plugin_instance->channel == nullptr) return;
  fl_method_channel_invoke_method(plugin_instance->channel, event_name, nullptr,
                                  nullptr, nullptr, nullptr);
}

static void show_context_menu() {
  if (menu == nullptr) return;
  gtk_widget_show_all(menu);
  gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static GtkWidget* find_menu_item_by_id(GtkWidget* widget, gint id) {
  if (widget == nullptr) return nullptr;
  if (GTK_IS_MENU_ITEM(widget) &&
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "tray-manager-id")) == id) {
    return widget;
  }
  if (GTK_IS_CONTAINER(widget)) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (GList* node = children; node != nullptr; node = node->next) {
      GtkWidget* found = find_menu_item_by_id(GTK_WIDGET(node->data), id);
      if (found != nullptr) {
        g_list_free(children);
        return found;
      }
      if (GTK_IS_MENU_ITEM(node->data)) {
        GtkWidget* submenu =
            gtk_menu_item_get_submenu(GTK_MENU_ITEM(node->data));
        if (submenu != nullptr) {
          found = find_menu_item_by_id(submenu, id);
          if (found != nullptr) {
            g_list_free(children);
            return found;
          }
        }
      }
    }
    g_list_free(children);
  }
  return nullptr;
}

static GVariant* dbusmenu_item_properties(GtkWidget* widget) {
  GVariantBuilder props;
  g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
  const bool separator = GTK_IS_SEPARATOR_MENU_ITEM(widget);
  const char* label = separator ? "" : gtk_menu_item_get_label(GTK_MENU_ITEM(widget));
  g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(label != nullptr ? label : ""));
  g_variant_builder_add(&props, "{sv}", "enabled", g_variant_new_boolean(gtk_widget_get_sensitive(widget)));
  g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(gtk_widget_get_visible(widget)));
  if (separator) {
    g_variant_builder_add(&props, "{sv}", "type", g_variant_new_string("separator"));
  } else if (GTK_IS_CHECK_MENU_ITEM(widget)) {
    g_variant_builder_add(&props, "{sv}", "toggle-type", g_variant_new_string("checkmark"));
    g_variant_builder_add(&props, "{sv}", "toggle-state",
                          g_variant_new_int32(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)) ? 1 : 0));
  }
  GtkWidget* submenu = GTK_IS_MENU_ITEM(widget)
                           ? gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget))
                           : nullptr;
  if (submenu != nullptr) {
    g_variant_builder_add(&props, "{sv}", "children-display", g_variant_new_string("submenu"));
  }
  return g_variant_builder_end(&props);
}

static GVariant* dbusmenu_layout(GtkWidget* widget, gint depth, gboolean root) {
  const gint id = root ? 0 : GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "tray-manager-id"));
  GVariantBuilder props;
  g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
  if (!root) {
    GVariant* properties = dbusmenu_item_properties(widget);
    GVariantIter iter;
    const gchar* key;
    GVariant* value;
    g_variant_iter_init(&iter, properties);
    while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
      g_variant_builder_add(&props, "{sv}", key, value);
      g_variant_unref(value);
    }
    g_variant_unref(properties);
  }

  GVariantBuilder children;
  g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
  GtkWidget* container = root ? menu :
      (GTK_IS_MENU_ITEM(widget) ? gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget)) : nullptr);
  if (container != nullptr && depth != 0 && GTK_IS_CONTAINER(container)) {
    GList* child_widgets = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList* node = child_widgets; node != nullptr; node = node->next) {
      GVariant* child = dbusmenu_layout(GTK_WIDGET(node->data), depth < 0 ? -1 : depth - 1, FALSE);
      g_variant_builder_add_value(&children, g_variant_new_variant(child));
      g_variant_unref(child);
    }
    g_list_free(child_widgets);
  }
  return g_variant_new("(i@a{sv}@av)", id, g_variant_builder_end(&props),
                       g_variant_builder_end(&children));
}

static GVariant* dbusmenu_get_property_for_item(GtkWidget* widget, const gchar* name) {
  if (strcmp(name, "label") == 0) {
    const char* label = GTK_IS_SEPARATOR_MENU_ITEM(widget) ? "" :
        gtk_menu_item_get_label(GTK_MENU_ITEM(widget));
    return g_variant_new_string(label != nullptr ? label : "");
  }
  if (strcmp(name, "enabled") == 0)
    return g_variant_new_boolean(gtk_widget_get_sensitive(widget));
  if (strcmp(name, "visible") == 0)
    return g_variant_new_boolean(gtk_widget_get_visible(widget));
  if (strcmp(name, "type") == 0 && GTK_IS_SEPARATOR_MENU_ITEM(widget))
    return g_variant_new_string("separator");
  if (strcmp(name, "toggle-type") == 0 && GTK_IS_CHECK_MENU_ITEM(widget))
    return g_variant_new_string("checkmark");
  if (strcmp(name, "toggle-state") == 0 && GTK_IS_CHECK_MENU_ITEM(widget))
    return g_variant_new_int32(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)) ? 1 : 0);
  if (strcmp(name, "children-display") == 0 && GTK_IS_MENU_ITEM(widget) &&
      gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget)) != nullptr)
    return g_variant_new_string("submenu");
  return nullptr;
}

static GVariant* dbusmenu_get_property(GDBusConnection* connection,
                                       const gchar* sender,
                                       const gchar* object_path,
                                       const gchar* interface_name,
                                       const gchar* property_name,
                                       GError** error,
                                       gpointer user_data) {
  if (strcmp(property_name, "Version") == 0) return g_variant_new_uint32(3);
  if (strcmp(property_name, "TextDirection") == 0) return g_variant_new_string("ltr");
  if (strcmp(property_name, "Status") == 0) return g_variant_new_string("normal");
  if (strcmp(property_name, "IconThemePath") == 0) return g_variant_new_strv(nullptr, 0);
  return nullptr;
}

static void dbusmenu_method_call(GDBusConnection* connection,
                                 const gchar* sender,
                                 const gchar* object_path,
                                 const gchar* interface_name,
                                 const gchar* method_name,
                                 GVariant* parameters,
                                 GDBusMethodInvocation* invocation,
                                 gpointer user_data) {
  if (strcmp(method_name, "GetLayout") == 0) {
    GVariant* depth_value = g_variant_get_child_value(parameters, 1);
    const gint depth = g_variant_get_int32(depth_value);
    g_variant_unref(depth_value);
    GVariant* layout = dbusmenu_layout(menu, depth, TRUE);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))", 1, layout));
  } else if (strcmp(method_name, "GetGroupProperties") == 0) {
    GVariantBuilder results;
    g_variant_builder_init(&results, G_VARIANT_TYPE("a(ia{sv})"));
    GVariant* ids = g_variant_get_child_value(parameters, 0);
    GVariantIter iter;
    gint id;
    g_variant_iter_init(&iter, ids);
    while (g_variant_iter_next(&iter, "i", &id)) {
      GtkWidget* item = find_menu_item_by_id(menu, id);
      if (item != nullptr) {
        g_variant_builder_add(&results, "(i@a{sv})", id, dbusmenu_item_properties(item));
      }
    }
    g_variant_unref(ids);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&results)));
  } else if (strcmp(method_name, "GetProperty") == 0) {
    GVariant* id_value = g_variant_get_child_value(parameters, 0);
    GVariant* name_value = g_variant_get_child_value(parameters, 1);
    const gint id = g_variant_get_int32(id_value);
    const gchar* name = g_variant_get_string(name_value, nullptr);
    GtkWidget* item = find_menu_item_by_id(menu, id);
    GVariant* value = item != nullptr ? dbusmenu_get_property_for_item(item, name) : nullptr;
    if (value == nullptr) {
      g_dbus_method_invocation_return_dbus_error(invocation, "com.canonical.dbusmenu.Error.UnknownProperty", "Unknown menu item property");
    } else {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", value));
    }
    g_clear_pointer(&value, g_variant_unref);
    g_variant_unref(id_value);
    g_variant_unref(name_value);
  } else if (strcmp(method_name, "Event") == 0) {
    GVariant* id_value = g_variant_get_child_value(parameters, 0);
    GVariant* event_value = g_variant_get_child_value(parameters, 1);
    const gint id = g_variant_get_int32(id_value);
    const gchar* event_name = g_variant_get_string(event_value, nullptr);
    GtkWidget* item = find_menu_item_by_id(menu, id);
    if (item != nullptr && strcmp(event_name, "clicked") == 0)
      gtk_menu_item_activate(GTK_MENU_ITEM(item));
    g_variant_unref(id_value);
    g_variant_unref(event_value);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
  } else if (strcmp(method_name, "EventGroup") == 0) {
    GVariantBuilder errors;
    g_variant_builder_init(&errors, G_VARIANT_TYPE("a(ias)"));
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ias))", g_variant_builder_end(&errors)));
  } else if (strcmp(method_name, "AboutToShow") == 0) {
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
  } else if (strcmp(method_name, "AboutToShowGroup") == 0) {
    GVariantBuilder updates;
    GVariantBuilder errors;
    g_variant_builder_init(&updates, G_VARIANT_TYPE("ai"));
    g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai@ai)", g_variant_builder_end(&updates), g_variant_builder_end(&errors)));
  }
}

static const GDBusInterfaceVTable dbusmenu_vtable = {
    dbusmenu_method_call, dbusmenu_get_property, nullptr, {0}};

static GVariant* sni_get_property(GDBusConnection* connection,
                                  const gchar* sender,
                                  const gchar* object_path,
                                  const gchar* interface_name,
                                  const gchar* property_name,
                                  GError** error,
                                  gpointer user_data) {
  if (strcmp(property_name, "Category") == 0)
    return g_variant_new_string("ApplicationStatus");
  if (strcmp(property_name, "Id") == 0)
    return g_variant_new_string("tray_manager");
  if (strcmp(property_name, "Title") == 0)
    return g_variant_new_string(tray_title != nullptr ? tray_title : "tray_manager");
  if (strcmp(property_name, "Status") == 0)
    return g_variant_new_string(tray_visible ? "Active" : "Passive");
  if (strcmp(property_name, "WindowId") == 0) return g_variant_new_int32(0);
  if (strcmp(property_name, "IconName") == 0) return g_variant_new_string("");
  if (strcmp(property_name, "IconPixmap") == 0)
    return build_icon_pixmaps(tray_icon_pixbuf);
  if (strcmp(property_name, "OverlayIconName") == 0 ||
      strcmp(property_name, "AttentionIconName") == 0 ||
      strcmp(property_name, "AttentionMovieName") == 0 ||
      strcmp(property_name, "XAyatanaLabel") == 0 ||
      strcmp(property_name, "XAyatanaLabelGuide") == 0)
    return g_variant_new_string("");
  if (strcmp(property_name, "OverlayIconPixmap") == 0 ||
      strcmp(property_name, "AttentionIconPixmap") == 0)
    return build_icon_pixmaps(nullptr);
  if (strcmp(property_name, "ToolTip") == 0) {
    return g_variant_new("(s@a(iiay)ss)", "",
                         build_icon_pixmaps(tray_icon_pixbuf),
                         tray_title != nullptr ? tray_title : "Commet", "");
  }
  if (strcmp(property_name, "ItemIsMenu") == 0)
    return g_variant_new_boolean(FALSE);
  if (strcmp(property_name, "Menu") == 0)
    return g_variant_new_object_path("/MenuBar");
  return nullptr;
}

static void sni_method_call(GDBusConnection* connection,
                            const gchar* sender,
                            const gchar* object_path,
                            const gchar* interface_name,
                            const gchar* method_name,
                            GVariant* parameters,
                            GDBusMethodInvocation* invocation,
                            gpointer user_data) {
  if (strcmp(method_name, "Activate") == 0) {
    emit_tray_event("onTrayIconMouseDown");
  } else if (strcmp(method_name, "ContextMenu") == 0 ||
             strcmp(method_name, "SecondaryActivate") == 0) {
    show_context_menu();
  }

  // Scroll is intentionally ignored; all SNI methods return an empty tuple.
  g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
}

static const GDBusInterfaceVTable sni_vtable = {
    sni_method_call,
    sni_get_property,
    nullptr,
    {0}};

static void watcher_registration_finished(GObject* source_object,
                                          GAsyncResult* result,
                                          gpointer user_data) {
  GError* error = nullptr;
  g_autoptr(GVariant) reply =
      g_dbus_connection_call_finish(G_DBUS_CONNECTION(source_object), result,
                                    &error);
  if (error != nullptr) {
    watcher_registered = FALSE;
    g_warning("tray_manager: StatusNotifierItem registration failed: %s",
              error->message);
    g_error_free(error);
    return;
  }
  watcher_registered = reply != nullptr;
}

static void watcher_name_appeared(GDBusConnection* connection,
                                  const gchar* name,
                                  const gchar* name_owner,
                                  gpointer user_data) {
  if (sni_connection == nullptr || watcher_registered) return;

  // A watcher can start after the app. Register whenever it appears, rather
  // than permanently losing the tray icon if the panel starts late.
  const gchar* unique_name = g_dbus_connection_get_unique_name(sni_connection);
  g_dbus_connection_call(
      sni_connection, name, "/StatusNotifierWatcher",
      "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
      g_variant_new("(s)", unique_name), nullptr, G_DBUS_CALL_FLAGS_NONE, 3000,
      nullptr, watcher_registration_finished, nullptr);
}

static void watcher_name_vanished(GDBusConnection* connection,
                                  const gchar* name,
                                  gpointer user_data) {
  watcher_registered = FALSE;
}

static gboolean register_status_notifier_item(GError** error) {
  sni_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, error);
  if (sni_connection == nullptr) return FALSE;

  sni_node_info = g_dbus_node_info_new_for_xml(kSniIntrospectionXml, error);
  if (sni_node_info == nullptr) return FALSE;

  sni_registration_id = g_dbus_connection_register_object(
      sni_connection, "/StatusNotifierItem",
      g_dbus_node_info_lookup_interface(sni_node_info,
                                        "org.kde.StatusNotifierItem"),
      &sni_vtable, nullptr, nullptr, error);
  if (sni_registration_id == 0) return FALSE;

  dbusmenu_node_info = g_dbus_node_info_new_for_xml(kDbusMenuIntrospectionXml, error);
  if (dbusmenu_node_info == nullptr) return FALSE;
  dbusmenu_registration_id = g_dbus_connection_register_object(
      sni_connection, "/MenuBar",
      g_dbus_node_info_lookup_interface(dbusmenu_node_info,
                                        "com.canonical.dbusmenu"),
      &dbusmenu_vtable, nullptr, nullptr, error);
  if (dbusmenu_registration_id == 0) return FALSE;

  watcher_watch_id = g_bus_watch_name_on_connection(
      sni_connection, "org.kde.StatusNotifierWatcher",
      G_BUS_NAME_WATCHER_FLAGS_NONE, watcher_name_appeared,
      watcher_name_vanished, nullptr, nullptr);
  return TRUE;
}

void _on_activate(GtkMenuItem* item, gpointer user_data) {
  const gint id = GPOINTER_TO_INT(user_data);

  g_autoptr(FlValue) result_data = fl_value_new_map();
  fl_value_set_string_take(result_data, "id", fl_value_new_int(id));
  fl_method_channel_invoke_method(plugin_instance->channel,
                                  "onTrayMenuItemClick", result_data, nullptr,
                                  nullptr, nullptr);
}

GtkWidget* _create_menu(FlValue* args) {
  FlValue* items_value = fl_value_lookup_string(args, "items");
  GtkWidget* new_menu = gtk_menu_new();

  for (gint i = 0; i < fl_value_get_length(items_value); i++) {
    FlValue* item_value = fl_value_get_list_value(items_value, i);
    const int id = fl_value_get_int(fl_value_lookup_string(item_value, "id"));
    const char* type =
        fl_value_get_string(fl_value_lookup_string(item_value, "type"));
    const char* label =
        fl_value_get_string(fl_value_lookup_string(item_value, "label"));
    const bool disabled =
        fl_value_get_bool(fl_value_lookup_string(item_value, "disabled"));

    GtkWidget* item = nullptr;
    if (strcmp(type, "separator") == 0) {
      item = gtk_separator_menu_item_new();
    } else {
      item = gtk_menu_item_new_with_label(label);
      if (disabled) gtk_widget_set_sensitive(item, FALSE);

      if (strcmp(type, "checkbox") == 0) {
        gtk_widget_destroy(item);
        item = gtk_check_menu_item_new_with_label(label);
        const auto checked_value =
            fl_value_lookup_string(item_value, "checked");
        if (checked_value != nullptr) {
          gtk_check_menu_item_set_active(
              GTK_CHECK_MENU_ITEM(item), fl_value_get_bool(checked_value));
        }
      } else if (strcmp(type, "submenu") == 0) {
        GtkWidget* sub_menu =
            _create_menu(fl_value_lookup_string(item_value, "submenu"));
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub_menu);
      }

      g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(_on_activate),
                       GINT_TO_POINTER(id));
    }
    g_object_set_data(G_OBJECT(item), "tray-manager-id", GINT_TO_POINTER(id));
    gtk_menu_shell_append(GTK_MENU_SHELL(new_menu), item);
  }
  return new_menu;
}

static FlMethodResponse* destroy(TrayManagerPlugin* self, FlValue* args) {
  tray_visible = FALSE;
  emit_sni_signal("NewStatus", g_variant_new("(s)", "Passive"));
  return FL_METHOD_RESPONSE(
      fl_method_success_response_new(fl_value_new_bool(true)));
}

static FlMethodResponse* set_icon(TrayManagerPlugin* self, FlValue* args) {
  const char* id = fl_value_get_string(fl_value_lookup_string(args, "id"));
  const char* icon_path =
      fl_value_get_string(fl_value_lookup_string(args, "iconPath"));
  (void)id;

  if (tray_icon_pixbuf != nullptr) {
    g_object_unref(tray_icon_pixbuf);
    tray_icon_pixbuf = nullptr;
  }

  if (icon_path != nullptr && *icon_path != '\0') {
    GError* icon_error = nullptr;
    tray_icon_pixbuf = gdk_pixbuf_new_from_file(icon_path, &icon_error);
    if (icon_error != nullptr) g_error_free(icon_error);

    // Sandboxed installs pass an icon name rather than a filesystem path.
    if (tray_icon_pixbuf == nullptr) {
      GtkIconTheme* theme = gtk_icon_theme_get_default();
      if (theme != nullptr) {
        gchar* basename = g_path_get_basename(icon_path);
        gchar* icon_name = g_strdup(basename);
        gchar* extension = strrchr(icon_name, '.');
        if (extension != nullptr) *extension = '\0';
        tray_icon_pixbuf =
            gtk_icon_theme_load_icon(theme, icon_name, 48,
                                     GTK_ICON_LOOKUP_FORCE_SIZE, nullptr);
        g_free(icon_name);
        g_free(basename);
      }
    }
  }

  if (sni_connection == nullptr) {
    GError* registration_error = nullptr;
    if (!register_status_notifier_item(&registration_error)) {
      g_warning("tray_manager: unable to register Linux StatusNotifierItem: %s",
                registration_error != nullptr ? registration_error->message
                                              : "unknown error");
      if (registration_error != nullptr) g_error_free(registration_error);
    }
  }

  tray_visible = TRUE;
  emit_sni_signal("NewIcon");
  emit_sni_signal("NewStatus", g_variant_new("(s)", "Active"));

  return FL_METHOD_RESPONSE(
      fl_method_success_response_new(fl_value_new_bool(true)));
}

static FlMethodResponse* set_title(TrayManagerPlugin* self, FlValue* args) {
  const char* title =
      fl_value_get_string(fl_value_lookup_string(args, "title"));
  g_free(tray_title);
  tray_title = g_strdup(title != nullptr ? title : "tray_manager");
  emit_sni_signal("NewTitle");
  emit_sni_signal("NewToolTip");
  return FL_METHOD_RESPONSE(
      fl_method_success_response_new(fl_value_new_bool(true)));
}

static FlMethodResponse* set_context_menu(TrayManagerPlugin* self,
                                          FlValue* args) {
  if (menu != nullptr) {
    gtk_widget_destroy(menu);
    menu = nullptr;
  }
  menu = _create_menu(fl_value_lookup_string(args, "menu"));
  gtk_widget_show_all(menu);
  if (sni_connection != nullptr) {
    g_dbus_connection_emit_signal(sni_connection, nullptr, "/MenuBar",
                                  "com.canonical.dbusmenu", "LayoutUpdated",
                                  g_variant_new("(ui)", 1, 0), nullptr);
  }

  return FL_METHOD_RESPONSE(
      fl_method_success_response_new(fl_value_new_bool(true)));
}

// Called when a method call is received from Flutter.
static void tray_manager_plugin_handle_method_call(TrayManagerPlugin* self,
                                                   FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;

  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* args = fl_method_call_get_args(method_call);

  if (strcmp(method, "destroy") == 0) {
    response = destroy(self, args);
  } else if (strcmp(method, "setIcon") == 0) {
    response = set_icon(self, args);
  } else if (strcmp(method, "setTitle") == 0) {
    response = set_title(self, args);
  } else if (strcmp(method, "setContextMenu") == 0) {
    response = set_context_menu(self, args);
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  fl_method_call_respond(method_call, response, nullptr);
}

static void tray_manager_plugin_dispose(GObject* object) {
  if (watcher_watch_id != 0) {
    g_bus_unwatch_name(watcher_watch_id);
    watcher_watch_id = 0;
  }
  if (sni_connection != nullptr && sni_registration_id != 0) {
    g_dbus_connection_unregister_object(sni_connection, sni_registration_id);
    sni_registration_id = 0;
  }
  if (sni_connection != nullptr && dbusmenu_registration_id != 0) {
    g_dbus_connection_unregister_object(sni_connection, dbusmenu_registration_id);
    dbusmenu_registration_id = 0;
  }
  if (dbusmenu_node_info != nullptr) {
    g_dbus_node_info_unref(dbusmenu_node_info);
    dbusmenu_node_info = nullptr;
  }
  if (sni_node_info != nullptr) {
    g_dbus_node_info_unref(sni_node_info);
    sni_node_info = nullptr;
  }
  if (sni_connection != nullptr) {
    g_object_unref(sni_connection);
    sni_connection = nullptr;
  }
  if (tray_icon_pixbuf != nullptr) {
    g_object_unref(tray_icon_pixbuf);
    tray_icon_pixbuf = nullptr;
  }
  g_clear_pointer(&tray_title, g_free);
  G_OBJECT_CLASS(tray_manager_plugin_parent_class)->dispose(object);
}

static void tray_manager_plugin_class_init(TrayManagerPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = tray_manager_plugin_dispose;
}

static void tray_manager_plugin_init(TrayManagerPlugin* self) {}

static void method_call_cb(FlMethodChannel* channel,
                           FlMethodCall* method_call,
                           gpointer user_data) {
  TrayManagerPlugin* plugin = TRAY_MANAGER_PLUGIN(user_data);
  tray_manager_plugin_handle_method_call(plugin, method_call);
}

void tray_manager_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  TrayManagerPlugin* plugin = TRAY_MANAGER_PLUGIN(
      g_object_new(tray_manager_plugin_get_type(), nullptr));

  plugin->registrar = FL_PLUGIN_REGISTRAR(g_object_ref(registrar));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  plugin->channel =
      fl_method_channel_new(fl_plugin_registrar_get_messenger(registrar),
                            "tray_manager", FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(
      plugin->channel, method_call_cb, g_object_ref(plugin), g_object_unref);

  plugin_instance = plugin;
  if (tray_title == nullptr) tray_title = g_strdup("tray_manager");

  g_object_unref(plugin);
}
