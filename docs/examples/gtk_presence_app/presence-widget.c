/*
 * presence-widget.c
 *
 * PresenceWidget
 *
 * Authors:
 *    Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <string.h>

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/gtypes.h>

#include "presence-widget.h"

#define GET_PRIVATE(obj)  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TYPE_PRESENCE_WIDGET, PresenceWidgetPrivate))

G_DEFINE_TYPE (PresenceWidget, presence_widget, GTK_TYPE_FRAME);

typedef struct _PresenceWidgetPrivate PresenceWidgetPrivate;
struct _PresenceWidgetPrivate
{
  TpAccount *account;
  GHashTable *statuses;

  GtkWidget *enabled_check;
  GtkWidget *status_icon;
  GtkWidget *status_message;

  gint updating_ui_lock;
};

enum /* properties */
{
  PROP_0,
  PROP_ACCOUNT
};

/* presence icons */
static const char *presence_icons[NUM_TP_CONNECTION_PRESENCE_TYPES] = {
    "empathy-offline",
    "empathy-offline",
    "empathy-available",
    "empathy-away",
    "empathy-extended-away",
    "empathy-offline",
    "empathy-busy",
    "empathy-offline",
    "empathy-offline",
};

static const char *default_messages[NUM_TP_CONNECTION_PRESENCE_TYPES] = {
    "Unset",
    "Offline",
    "Available",
    "Away",
    "Extended Away",
    "Hidden",
    "Busy",
    "Unknown",
    "Error"
};

static void
presence_widget_get_property (GObject    *self,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  switch (prop_id)
    {
      case PROP_ACCOUNT:
        g_value_set_object (value, priv->account);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
        break;
    }
}

static void
presence_widget_set_property (GObject      *self,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  switch (prop_id)
    {
      case PROP_ACCOUNT:
        priv->account = g_value_dup_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
        break;
    }
}

static void
_notify_enabled (PresenceWidget *self,
                 GParamSpec     *pspec,
                 TpAccount      *account)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  priv->updating_ui_lock++;
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->enabled_check),
      tp_account_is_enabled (account));
  priv->updating_ui_lock--;
}

static void
_notify_display_name (PresenceWidget *self,
                      GParamSpec     *pspec,
                      TpAccount      *account)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  gtk_button_set_label (GTK_BUTTON (priv->enabled_check),
      tp_account_get_display_name (account));
}

static void
_notify_presence (PresenceWidget *self,
                  GParamSpec     *pspec,
                  TpAccount      *account)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);
  TpConnectionPresenceType presence = tp_account_get_presence (account);

  const char *icon_name = presence_icons[presence];

  gtk_image_set_from_icon_name (GTK_IMAGE (priv->status_icon),
      icon_name, GTK_ICON_SIZE_MENU);
}

static void
_notify_status_message (PresenceWidget *self,
                        GParamSpec     *pspec,
                        TpAccount      *account)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);
  const char *msg = tp_account_get_status_message (account);

  if (strlen (msg) == 0)
    {
      TpConnectionPresenceType presence = tp_account_get_presence (account);
      msg = default_messages[presence];
    }

  gtk_label_set_text (GTK_LABEL (priv->status_message), msg);
}

static void
_get_property_statuses (TpProxy      *conn,
                        const GValue *value,
                        const GError *error,
                        gpointer      user_data,
                        GObject      *self)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  if (error != NULL)
    {
      g_warning ("%s", error->message);
      return;
    }

  g_return_if_fail (G_VALUE_HOLDS (value, TP_HASH_TYPE_SIMPLE_STATUS_SPEC_MAP));

  if (priv->statuses != NULL) g_hash_table_unref (priv->statuses);
  priv->statuses = g_hash_table_ref (g_value_get_boxed (value));

  // FIXME: do I need to hold onto this?
}

static void
_connection_ready (TpConnection *conn,
                   const GError *error,
                   gpointer      user_data)
{
  PresenceWidget *self = PRESENCE_WIDGET (user_data);
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  if (error != NULL)
    {
      g_warning ("%s", error->message);
      return;
    }

  if (tp_proxy_has_interface (conn,
        TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE))
    {
      /* request the Statuses property */
      tp_cli_dbus_properties_call_get (conn, -1,
          TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
          "Statuses",
          _get_property_statuses,
          NULL, NULL, G_OBJECT (self));
    }
}

static void
_status_changed (PresenceWidget *self,
                 guint           old_status,
                 guint           new_status,
                 guint           reason,
                 TpAccount      *account)
{
  TpConnection *conn = tp_account_get_connection (account);

  if (conn == NULL) return;
  else if (new_status == TP_CONNECTION_STATUS_CONNECTED ||
           new_status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      tp_connection_call_when_ready (conn, _connection_ready, self);
    }
}

static void
_account_removed (PresenceWidget *self,
                  TpAccount      *account)
{
  /* this account has been removed, destroy ourselves */
  gtk_widget_destroy (GTK_WIDGET (self));
}

static void
presence_widget_constructed (GObject *self)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  g_signal_connect_swapped (priv->account, "notify::enabled",
      G_CALLBACK (_notify_enabled), self);
  g_signal_connect_swapped (priv->account, "notify::display-name",
      G_CALLBACK (_notify_display_name), self);
  g_signal_connect_swapped (priv->account, "notify::presence",
      G_CALLBACK (_notify_presence), self);
  g_signal_connect_swapped (priv->account, "notify::status-message",
      G_CALLBACK (_notify_status_message), self);

  g_signal_connect_swapped (priv->account, "status-changed",
      G_CALLBACK (_status_changed), self);
  g_signal_connect_swapped (priv->account, "removed",
      G_CALLBACK (_account_removed), self);

  _notify_enabled (PRESENCE_WIDGET (self), NULL, priv->account);
  _notify_display_name (PRESENCE_WIDGET (self), NULL, priv->account);
  _notify_presence (PRESENCE_WIDGET (self), NULL, priv->account);
  _notify_status_message (PRESENCE_WIDGET (self), NULL, priv->account);

  _status_changed (PRESENCE_WIDGET (self), 0,
      tp_account_get_connection_status (priv->account),
      0, priv->account);
}

static void
presence_widget_dispose (GObject *self)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  g_object_unref (priv->account);
  priv->account = NULL;

  G_OBJECT_CLASS (presence_widget_parent_class)->dispose (self);
}

static void
presence_widget_class_init (PresenceWidgetClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);

  gobject_class->constructed  = presence_widget_constructed;
  gobject_class->dispose      = presence_widget_dispose;
  gobject_class->get_property = presence_widget_get_property;
  gobject_class->set_property = presence_widget_set_property;

  g_object_class_install_property (gobject_class,
      PROP_ACCOUNT,
      g_param_spec_object ("account",
                           "account",
                           "Telepathy Account",
                           TP_TYPE_ACCOUNT,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_type_class_add_private (gobject_class, sizeof (PresenceWidgetPrivate));
}

static void
_enabled_toggled (PresenceWidget  *self,
                  GtkToggleButton *button)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  if (priv->updating_ui_lock > 0) return;

  tp_account_set_enabled_async (priv->account,
      gtk_toggle_button_get_active (button),
      NULL, NULL);
}

static void
presence_widget_init (PresenceWidget *self)
{
  PresenceWidgetPrivate *priv = GET_PRIVATE (self);

  priv->enabled_check = gtk_check_button_new ();
  gtk_frame_set_label_widget (GTK_FRAME (self), priv->enabled_check);
  g_signal_connect_swapped (priv->enabled_check, "toggled",
      G_CALLBACK (_enabled_toggled), self);

  GtkWidget *table = gtk_table_new (2, 2, FALSE);
  gtk_container_add (GTK_CONTAINER (self), table);
  gtk_container_set_border_width (GTK_CONTAINER (table), 3);
  gtk_table_set_col_spacings (GTK_TABLE (table), 3);

  priv->status_icon = gtk_image_new ();
  gtk_table_attach (GTK_TABLE (table), priv->status_icon,
      0, 1, 0, 1,
      GTK_FILL, GTK_FILL, 0, 0);

  priv->status_message = gtk_label_new ("");
  gtk_table_attach (GTK_TABLE (table), priv->status_message,
      1, 2, 0, 1,
      GTK_FILL, GTK_FILL, 0, 0);

  gtk_widget_show (priv->enabled_check);
  gtk_widget_show_all (table);
}

GtkWidget *
presence_widget_new (TpAccount *account)
{
  g_return_val_if_fail (TP_IS_ACCOUNT (account), NULL);

  return g_object_new (TYPE_PRESENCE_WIDGET,
      "account", account,
      NULL);
}
