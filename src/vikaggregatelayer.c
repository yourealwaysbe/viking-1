/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 * Copyright (C) 2013-2015, Rob Norris <rw_norris@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "viking.h"
#include "viktrwlayer_analysis.h"
#include "viktrwlayer_tracklist.h"
#include "viktrwlayer_waypointlist.h"
#include "icons/icons.h"

#include <string.h>
#include <glib/gi18n.h>

static void aggregate_layer_marshall( VikAggregateLayer *val, guint8 **data, guint *len );
static VikAggregateLayer *aggregate_layer_unmarshall( guint8 *data, guint len, VikViewport *vvp );
static void aggregate_layer_change_coord_mode ( VikAggregateLayer *val, VikCoordMode mode );
static void aggregate_layer_drag_drop_request ( VikAggregateLayer *val_src, VikAggregateLayer *val_dest, GtkTreeIter *src_item_iter, GtkTreePath *dest_path );
static const gchar* aggregate_layer_tooltip ( VikAggregateLayer *val );
static void aggregate_layer_add_menu_items ( VikAggregateLayer *val, GtkMenu *menu, gpointer vlp );

VikLayerInterface vik_aggregate_layer_interface = {
  "Aggregate",
  N_("Aggregate"),
  "<control><shift>A",
  &vikaggregatelayer_pixbuf,

  NULL,
  0,

  NULL,
  0,
  NULL,
  0,

  VIK_MENU_ITEM_ALL,

  (VikLayerFuncCreate)                  vik_aggregate_layer_create,
  (VikLayerFuncRealize)                 vik_aggregate_layer_realize,
  (VikLayerFuncPostRead)                NULL,
  (VikLayerFuncFree)                    vik_aggregate_layer_free,

  (VikLayerFuncProperties)              NULL,
  (VikLayerFuncDraw)                    vik_aggregate_layer_draw,
  (VikLayerFuncChangeCoordMode)         aggregate_layer_change_coord_mode,
  
  (VikLayerFuncGetTimestamp)            NULL,

  (VikLayerFuncSetMenuItemsSelection)	NULL,
  (VikLayerFuncGetMenuItemsSelection)	NULL,

  (VikLayerFuncAddMenuItems)            aggregate_layer_add_menu_items,
  (VikLayerFuncSublayerAddMenuItems)    NULL,

  (VikLayerFuncSublayerRenameRequest)   NULL,
  (VikLayerFuncSublayerToggleVisible)   NULL,
  (VikLayerFuncSublayerTooltip)         NULL,
  (VikLayerFuncLayerTooltip)            aggregate_layer_tooltip,
  (VikLayerFuncLayerSelected)           NULL,

  (VikLayerFuncMarshall)		aggregate_layer_marshall,
  (VikLayerFuncUnmarshall)		aggregate_layer_unmarshall,

  (VikLayerFuncSetParam)                NULL,
  (VikLayerFuncGetParam)                NULL,
  (VikLayerFuncChangeParam)             NULL,

  (VikLayerFuncReadFileData)            NULL,
  (VikLayerFuncWriteFileData)           NULL,

  (VikLayerFuncDeleteItem)              NULL,
  (VikLayerFuncCutItem)                 NULL,
  (VikLayerFuncCopyItem)                NULL,
  (VikLayerFuncPasteItem)               NULL,
  (VikLayerFuncFreeCopiedItem)          NULL,
  (VikLayerFuncDragDropRequest)		aggregate_layer_drag_drop_request,

  (VikLayerFuncSelectClick)             NULL,
  (VikLayerFuncSelectMove)              NULL,
  (VikLayerFuncSelectRelease)           NULL,
  (VikLayerFuncSelectedViewportMenu)    NULL,

  (VikLayerFuncRefresh)                 NULL,
};

struct _VikAggregateLayer {
  VikLayer vl;
  GList *children;
  // One per layer
  GtkWidget *tracks_analysis_dialog;
};

GType vik_aggregate_layer_get_type ()
{
  static GType val_type = 0;

  if (!val_type)
  {
    static const GTypeInfo val_info =
    {
      sizeof (VikAggregateLayerClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      NULL, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (VikAggregateLayer),
      0,
      NULL /* instance init */
    };
    val_type = g_type_register_static ( VIK_LAYER_TYPE, "VikAggregateLayer", &val_info, 0 );
  }

  return val_type;
}

VikAggregateLayer *vik_aggregate_layer_create (VikViewport *vp)
{
  VikAggregateLayer *rv = vik_aggregate_layer_new ();
  vik_layer_rename ( VIK_LAYER(rv), vik_aggregate_layer_interface.name );
  return rv;
}

static void aggregate_layer_marshall( VikAggregateLayer *val, guint8 **data, guint *datalen )
{
  GList *child = val->children;
  VikLayer *child_layer;
  guint8 *ld; 
  guint ll;
  GByteArray* b = g_byte_array_new ();
  guint len;

#define alm_append(obj, sz) 	\
  len = (sz);    		\
  g_byte_array_append ( b, (guint8 *)&len, sizeof(len) );	\
  g_byte_array_append ( b, (guint8 *)(obj), len );

  vik_layer_marshall_params(VIK_LAYER(val), &ld, &ll);
  alm_append(ld, ll);
  g_free(ld);

  while (child) {
    child_layer = VIK_LAYER(child->data);
    vik_layer_marshall ( child_layer, &ld, &ll );
    if (ld) {
      alm_append(ld, ll);
      g_free(ld);
    }
    child = child->next;
  }
  *data = b->data;
  *datalen = b->len;
  g_byte_array_free(b, FALSE);
#undef alm_append
}

static VikAggregateLayer *aggregate_layer_unmarshall( guint8 *data, guint len, VikViewport *vvp )
{
#define alm_size (*(guint *)data)
#define alm_next \
  len -= sizeof(gint) + alm_size; \
  data += sizeof(gint) + alm_size;
  
  VikAggregateLayer *rv = vik_aggregate_layer_new();
  VikLayer *child_layer;

  vik_layer_unmarshall_params ( VIK_LAYER(rv), data+sizeof(guint), alm_size, vvp );
  alm_next;

  while (len>0) {
    child_layer = vik_layer_unmarshall ( data + sizeof(guint), alm_size, vvp );
    if (child_layer) {
      rv->children = g_list_append ( rv->children, child_layer );
      g_signal_connect_swapped ( G_OBJECT(child_layer), "update", G_CALLBACK(vik_layer_emit_update_secondary), rv );
    }
    alm_next;
  }
  //  g_print("aggregate_layer_unmarshall ended with len=%d\n", len);
  return rv;
#undef alm_size
#undef alm_next
}

VikAggregateLayer *vik_aggregate_layer_new ()
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( g_object_new ( VIK_AGGREGATE_LAYER_TYPE, NULL ) );
  vik_layer_set_type ( VIK_LAYER(val), VIK_LAYER_AGGREGATE );
  val->children = NULL;
  return val;
}

void vik_aggregate_layer_insert_layer ( VikAggregateLayer *val, VikLayer *l, GtkTreeIter *replace_iter )
{
  GtkTreeIter iter;
  VikLayer *vl = VIK_LAYER(val);

  // By default layers are inserted above the selected layer
  gboolean put_above = TRUE;

  // These types are 'base' types in that you what other information on top
  if ( l->type == VIK_LAYER_MAPS || l->type == VIK_LAYER_DEM || l->type == VIK_LAYER_GEOREF )
    put_above = FALSE;

  if ( vl->realized )
  {
    vik_treeview_insert_layer ( vl->vt, &(vl->iter), &iter, l->name, val, put_above, l, l->type, l->type, replace_iter, vik_layer_get_timestamp(l) );
    if ( ! l->visible )
      vik_treeview_item_set_visible ( vl->vt, &iter, FALSE );
    vik_layer_realize ( l, vl->vt, &iter );

    if ( val->children == NULL )
      vik_treeview_expand ( vl->vt, &(vl->iter) );
  }

  if (replace_iter) {
    GList *theone = g_list_find ( val->children, vik_treeview_item_get_pointer ( vl->vt, replace_iter ) );
    if ( put_above )
      val->children = g_list_insert ( val->children, l, g_list_position(val->children,theone)+1 );
    else
      // Thus insert 'here' (so don't add 1)
      val->children = g_list_insert ( val->children, l, g_list_position(val->children,theone) );
  } else {
    // Effectively insert at 'end' of the list to match how displayed in the treeview
    //  - but since it is drawn from 'bottom first' it is actually the first in the child list
    // This ordering is especially important if it is a map or similar type,
    //  which needs be drawn first for the layering draw method to work properly.
    // ATM this only happens when a layer is drag/dropped to the end of an aggregate layer
    val->children = g_list_prepend ( val->children, l );
  }
  g_signal_connect_swapped ( G_OBJECT(l), "update", G_CALLBACK(vik_layer_emit_update_secondary), val );
}

/**
 * vik_aggregate_layer_add_layer:
 * @allow_reordering: should be set for GUI interactions,
 *                    whereas loading from a file needs strict ordering and so should be FALSE
 */
void vik_aggregate_layer_add_layer ( VikAggregateLayer *val, VikLayer *l, gboolean allow_reordering )
{
  GtkTreeIter iter;
  VikLayer *vl = VIK_LAYER(val);

  // By default layers go to the top
  gboolean put_above = TRUE;

  if ( allow_reordering ) {
    // These types are 'base' types in that you what other information on top
    if ( l->type == VIK_LAYER_MAPS || l->type == VIK_LAYER_DEM || l->type == VIK_LAYER_GEOREF )
      put_above = FALSE;
  }

  if ( vl->realized )
  {
    vik_treeview_add_layer ( vl->vt, &(vl->iter), &iter, l->name, val, put_above, l, l->type, l->type, vik_layer_get_timestamp(l) );
    if ( ! l->visible )
      vik_treeview_item_set_visible ( vl->vt, &iter, FALSE );
    vik_layer_realize ( l, vl->vt, &iter );

    if ( val->children == NULL )
      vik_treeview_expand ( vl->vt, &(vl->iter) );
  }

  if ( put_above )
    val->children = g_list_append ( val->children, l );
  else
    val->children = g_list_prepend ( val->children, l );

  g_signal_connect_swapped ( G_OBJECT(l), "update", G_CALLBACK(vik_layer_emit_update_secondary), val );
}

void vik_aggregate_layer_move_layer ( VikAggregateLayer *val, GtkTreeIter *child_iter, gboolean up )
{
  GList *theone, *first, *second;
  VikLayer *vl = VIK_LAYER(val);
  vik_treeview_move_item ( vl->vt, child_iter, up );

  theone = g_list_find ( val->children, vik_treeview_item_get_pointer ( vl->vt, child_iter ) );

  g_assert ( theone != NULL );

  /* the old switcheroo */
  if ( up && theone->next )
  {
    first = theone;
    second = theone->next;
  }
  else if ( !up && theone->prev )
  {
    first = theone->prev;
    second = theone;
  }
  else
    return;

  first->next = second->next;
  second->prev = first->prev;
  first->prev = second;
  second->next = first;

  /* second is now first */

  if ( second->prev )
    second->prev->next = second;
  if ( first->next )
    first->next->prev = first;

  if ( second->prev == NULL )
    val->children = second;
}

/* Draw the aggregate layer. If vik viewport is in half_drawn mode, this means we are only
 * to draw the layers above and including the trigger layer.
 * To do this we don't draw any layers if in half drawn mode, unless we find the
 * trigger layer, in which case we pull up the saved pixmap, turn off half drawn mode and
 * start drawing layers.
 * Also, if we were never in half drawn mode, we save a snapshot
 * of the pixmap before drawing the trigger layer so we can use it again
 * later.
 */
void vik_aggregate_layer_draw ( VikAggregateLayer *val, VikViewport *vp )
{
  GList *iter = val->children;
  VikLayer *vl;
  VikLayer *trigger = VIK_LAYER(vik_viewport_get_trigger( vp ));
  while ( iter ) {
    vl = VIK_LAYER(iter->data);
    if ( vl == trigger ) {
      if ( vik_viewport_get_half_drawn ( vp ) ) {
        vik_viewport_set_half_drawn ( vp, FALSE );
        vik_viewport_snapshot_load( vp );
      } else {
        vik_viewport_snapshot_save( vp );
      }
    }
    if ( vl->type == VIK_LAYER_AGGREGATE || vl->type == VIK_LAYER_GPS || ! vik_viewport_get_half_drawn( vp ) )
      vik_layer_draw ( vl, vp );
    iter = iter->next;
  }
}

static void aggregate_layer_change_coord_mode ( VikAggregateLayer *val, VikCoordMode mode )
{
  GList *iter = val->children;
  while ( iter )
  {
    vik_layer_change_coord_mode ( VIK_LAYER(iter->data), mode );
    iter = iter->next;
  }
}

// A slightly better way of defining the menu callback information
// This should be easier to extend/rework compared to previously
typedef enum {
  MA_VAL = 0,
  MA_VLP,
  MA_LAST
} menu_array_index;

typedef gpointer menu_array_values[MA_LAST];

static void aggregate_layer_child_visible_toggle ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  VikLayersPanel *vlp = VIK_LAYERS_PANEL ( values[MA_VLP] );
  VikLayer *vl;

  // Loop around all (child) layers applying visibility setting
  // This does not descend the tree if there are aggregates within aggregate - just the first level of layers held
  GList *iter = val->children;
  while ( iter ) {
    vl = VIK_LAYER ( iter->data );
    vl->visible = !vl->visible;
    // Also set checkbox on/off
    vik_treeview_item_toggle_visible ( vik_layers_panel_get_treeview ( vlp ), &(vl->iter) );
    iter = iter->next;
  }
  // Redraw as view may have changed
  vik_layer_emit_update ( VIK_LAYER ( val ) );
}

static void aggregate_layer_child_visible ( menu_array_values values, gboolean on_off)
{
  // Convert data back to correct types
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  VikLayersPanel *vlp = VIK_LAYERS_PANEL ( values[MA_VLP] );
  VikLayer *vl;

  // Loop around all (child) layers applying visibility setting
  // This does not descend the tree if there are aggregates within aggregate - just the first level of layers held
  GList *iter = val->children;
  while ( iter ) {
    vl = VIK_LAYER ( iter->data );
    vl->visible = on_off;
    // Also set checkbox on_off
    vik_treeview_item_set_visible ( vik_layers_panel_get_treeview ( vlp ), &(vl->iter), on_off );
    iter = iter->next;
  }
  // Redraw as view may have changed
  vik_layer_emit_update ( VIK_LAYER ( val ) );
}

static void aggregate_layer_child_visible_on ( menu_array_values values )
{
  aggregate_layer_child_visible ( values, TRUE );
}

static void aggregate_layer_child_visible_off ( menu_array_values values )
{
  aggregate_layer_child_visible ( values, FALSE );
}

/**
 * If order is true sort ascending, otherwise a descending sort
 */
static gint sort_layer_compare ( gconstpointer a, gconstpointer b, gpointer order )
{
  VikLayer *sa = (VikLayer *)a;
  VikLayer *sb = (VikLayer *)b;

  // Default ascending order
  gint answer = g_strcmp0 ( sa->name, sb->name );

  if ( GPOINTER_TO_INT(order) ) {
    // Invert sort order for ascending order
    answer = -answer;
  }

  return answer;
}

static void aggregate_layer_sort_a2z ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  vik_treeview_sort_children ( VIK_LAYER(val)->vt, &(VIK_LAYER(val)->iter), VL_SO_ALPHABETICAL_ASCENDING );
  val->children = g_list_sort_with_data ( val->children, sort_layer_compare, GINT_TO_POINTER(TRUE) );
}

static void aggregate_layer_sort_z2a ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  vik_treeview_sort_children ( VIK_LAYER(val)->vt, &(VIK_LAYER(val)->iter), VL_SO_ALPHABETICAL_DESCENDING );
  val->children = g_list_sort_with_data ( val->children, sort_layer_compare, GINT_TO_POINTER(FALSE) );
}

/**
 * If order is true sort ascending, otherwise a descending sort
 */
static gint sort_layer_compare_timestamp ( gconstpointer a, gconstpointer b, gpointer order )
{
  VikLayer *sa = (VikLayer *)a;
  VikLayer *sb = (VikLayer *)b;

  // Default ascending order
  // NB This might be relatively slow...
  gint answer = ( vik_layer_get_timestamp(sa) > vik_layer_get_timestamp(sb) );

  if ( GPOINTER_TO_INT(order) ) {
    // Invert sort order for ascending order
    answer = !answer;
  }

  return answer;
}

static void aggregate_layer_sort_timestamp_ascend ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  vik_treeview_sort_children ( VIK_LAYER(val)->vt, &(VIK_LAYER(val)->iter), VL_SO_DATE_ASCENDING );
  val->children = g_list_sort_with_data ( val->children, sort_layer_compare_timestamp, GINT_TO_POINTER(TRUE) );
}

static void aggregate_layer_sort_timestamp_descend ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  vik_treeview_sort_children ( VIK_LAYER(val)->vt, &(VIK_LAYER(val)->iter), VL_SO_DATE_DESCENDING );
  val->children = g_list_sort_with_data ( val->children, sort_layer_compare_timestamp, GINT_TO_POINTER(FALSE) );
}

/**
 * aggregate_layer_waypoint_create_list:
 * @vl:        The layer that should create the waypoint and layers list
 * @user_data: Not used in this function
 *
 * Returns: A list of #vik_trw_waypoint_list_t
 */
static GList* aggregate_layer_waypoint_create_list ( VikLayer *vl, gpointer user_data )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER(vl);

  // Get all TRW layers
  GList *layers = NULL;
  layers = vik_aggregate_layer_get_all_layers_of_type ( val, layers, VIK_LAYER_TRW, TRUE );

  // For each TRW layers keep adding the waypoints to build a list of all of them
  GList *waypoints_and_layers = NULL;
  layers = g_list_first ( layers );
  while ( layers ) {
    GList *waypoints = NULL;
    waypoints = g_list_concat ( waypoints, g_hash_table_get_values ( vik_trw_layer_get_waypoints( VIK_TRW_LAYER(layers->data) ) ) );

    waypoints_and_layers = g_list_concat ( waypoints_and_layers, vik_trw_layer_build_waypoint_list_t ( VIK_TRW_LAYER(layers->data), waypoints ) );

    layers = g_list_next ( layers );
  }
  g_list_free ( layers );

  return waypoints_and_layers;
}

static void aggregate_layer_waypoint_list_dialog ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  gchar *title = g_strdup_printf ( _("%s: Waypoint List"), VIK_LAYER(val)->name );
  vik_trw_layer_waypoint_list_show_dialog ( title, VIK_LAYER(val), NULL, aggregate_layer_waypoint_create_list, TRUE );
  g_free ( title );
}

/**
 *
 */
gboolean vik_aggregate_layer_search_date ( VikAggregateLayer *val, gchar *date_str )
{
  gboolean found = FALSE;
  VikViewport *vvp = vik_window_viewport ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(val)) );

  VikCoord position;
  GList *layers = NULL;
  layers = vik_aggregate_layer_get_all_layers_of_type ( val, layers, VIK_LAYER_TRW, TRUE );
  GList *gl = layers;
  // Search tracks first
  while ( gl && !found ) {
    // Make it auto select the item if found
    found = vik_trw_layer_find_date ( VIK_TRW_LAYER(gl->data), date_str, &position, vvp, TRUE, TRUE );
    gl = g_list_next ( gl );
  }
  if ( !found ) {
    // Reset and try on Waypoints
    gl = g_list_first ( layers );
    while ( gl && !found ) {
      // Make it auto select the item if found
      found = vik_trw_layer_find_date ( VIK_TRW_LAYER(gl->data), date_str, &position, vvp, FALSE, TRUE );
      gl = g_list_next ( gl );
    }
  }
  g_list_free ( layers );

  return found;
}
/**
 * Search all TrackWaypoint layers in this aggregate layer for an item on the user specified date
 */
static void aggregate_layer_search_date ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  gchar *date_str = a_dialog_get_date ( VIK_GTK_WINDOW_FROM_LAYER(val), _("Search by Date") );
  if ( !date_str )
    return;

  gboolean found = vik_aggregate_layer_search_date ( val, date_str );
  if ( !found )
    a_dialog_info_msg ( VIK_GTK_WINDOW_FROM_LAYER(val), _("No items found with the requested date.") );
  g_free ( date_str );
}

/**
 * aggregate_layer_track_create_list:
 * @vl:        The layer that should create the track and layers list
 * @user_data: Not used in this function
 *
 * Returns: A list of #vik_trw_and_track_t
 */
static GList* aggregate_layer_track_create_list ( VikLayer *vl, gpointer user_data )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER(vl);

  // Get all TRW layers
  GList *layers = NULL;
  layers = vik_aggregate_layer_get_all_layers_of_type ( val, layers, VIK_LAYER_TRW, TRUE );

  // For each TRW layers keep adding the tracks and routes to build a list of all of them
  GList *tracks_and_layers = NULL;
  layers = g_list_first ( layers );
  while ( layers ) {
    GList *tracks = NULL;
    tracks = g_list_concat ( tracks, g_hash_table_get_values ( vik_trw_layer_get_tracks( VIK_TRW_LAYER(layers->data) ) ) );
    tracks = g_list_concat ( tracks, g_hash_table_get_values ( vik_trw_layer_get_routes( VIK_TRW_LAYER(layers->data) ) ) );

    tracks_and_layers = g_list_concat ( tracks_and_layers, vik_trw_layer_build_track_list_t ( VIK_TRW_LAYER(layers->data), tracks ) );

    layers = g_list_next ( layers );
  }
  g_list_free ( layers );

  return tracks_and_layers;
}

static void aggregate_layer_track_list_dialog ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  gchar *title = g_strdup_printf ( _("%s: Track and Route List"), VIK_LAYER(val)->name );
  vik_trw_layer_track_list_show_dialog ( title, VIK_LAYER(val), NULL, aggregate_layer_track_create_list, TRUE );
  g_free ( title );
}

/**
 * aggregate_layer_analyse_close:
 *
 * Stuff to do on dialog closure
 */
static void aggregate_layer_analyse_close ( GtkWidget *dialog, gint resp, VikLayer* vl )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER(vl);
  gtk_widget_destroy ( dialog );
  val->tracks_analysis_dialog = NULL;
}

static void aggregate_layer_analyse ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );

  // There can only be one!
  if ( val->tracks_analysis_dialog )
    return;

  val->tracks_analysis_dialog = vik_trw_layer_analyse_this ( VIK_GTK_WINDOW_FROM_LAYER(VIK_LAYER(val)),
                                                             VIK_LAYER(val)->name,
                                                             VIK_LAYER(val),
                                                             NULL,
                                                             aggregate_layer_track_create_list,
                                                             aggregate_layer_analyse_close );
}

static void aggregate_layer_load_external_layers ( VikAggregateLayer *val )
{
  GList *iter = val->children;
  while ( iter ) {
    VikLayer *vl = VIK_LAYER ( iter->data );
    g_debug ( "child %d",  vl->type );
    switch ( vl->type ) {
      case VIK_LAYER_TRW: trw_ensure_layer_loaded ( VIK_TRW_LAYER ( iter->data ) ); break;
      case VIK_LAYER_AGGREGATE: aggregate_layer_load_external_layers ( VIK_AGGREGATE_LAYER ( iter->data ) ); break;
      default: /* do nothing */ break;
    }
    iter = iter->next;
  }
}

static void aggregate_layer_load_external_layers_click ( menu_array_values values )
{
  aggregate_layer_load_external_layers ( VIK_AGGREGATE_LAYER ( values[MA_VAL] ) );
  vik_layers_panel_calendar_update ( values[MA_VLP] );
}

/**
 * aggregate_layer_file_load:
 *
 * Asks the user to select files and then load them into this aggregate layer
 */
static void aggregate_layer_file_load ( menu_array_values values )
{
  VikAggregateLayer *val = VIK_AGGREGATE_LAYER ( values[MA_VAL] );
  VikWindow *vw = VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(val));
  VikViewport *vvp = vik_window_viewport ( vw );

  gchar *filename = NULL;
  GSList *files = vu_get_ui_selected_gps_files ( vw, TRUE ); // Only GPX types for the filter type ATM

  if ( files ) {
    GSList *cur_file = files;
    while ( cur_file ) {
      filename = cur_file->data;

      VikLoadType_t ans = a_file_load ( val, vvp, NULL, filename, TRUE, FALSE, NULL );
      if ( ans <= LOAD_TYPE_UNSUPPORTED_FAILURE ) {
        a_dialog_error_msg_extra ( GTK_WINDOW(vw), _("Unable to load %s"), filename );
      } else if ( ans <= LOAD_TYPE_VIK_FAILURE_NON_FATAL ) {
        gchar *msg = g_strdup_printf (_("WARNING: issues encountered loading %s"), a_file_basename(filename) );
        vik_window_statusbar_update ( vw, msg, VIK_STATUSBAR_INFO );
        g_free ( msg );
      }
      g_free ( filename );
      cur_file = g_slist_next ( cur_file );
    }
    g_slist_free ( files );
  }

  vik_layer_emit_update ( VIK_LAYER ( val ) );
}

static void aggregate_layer_add_menu_items ( VikAggregateLayer *val, GtkMenu *menu, gpointer vlp )
{
  // Data to pass on in menu functions
  static menu_array_values values;
  values[MA_VAL] = val;
  values[MA_VLP] = vlp;

  (void)vu_menu_add_item ( menu, NULL, NULL, NULL, NULL ); // Just a separator

  GtkMenu *vis_submenu = GTK_MENU(gtk_menu_new());
  GtkWidget *itemv = vu_menu_add_item ( menu, _("_Visibility"), NULL, NULL, NULL );
  gtk_menu_item_set_submenu ( GTK_MENU_ITEM(itemv), GTK_WIDGET(vis_submenu) );

  (void)vu_menu_add_item ( vis_submenu, _("_Show All"), GTK_STOCK_APPLY, G_CALLBACK(aggregate_layer_child_visible_on), values );
  (void)vu_menu_add_item ( vis_submenu, _("_Hide All"), GTK_STOCK_CLEAR, G_CALLBACK(aggregate_layer_child_visible_off), values );
  (void)vu_menu_add_item ( vis_submenu, _("_Toggle"), GTK_STOCK_REFRESH, G_CALLBACK(aggregate_layer_child_visible_toggle), values );

  GtkMenu *submenu_sort = GTK_MENU(gtk_menu_new());
  GtkWidget *items = vu_menu_add_item ( menu, _("_Sort"), GTK_STOCK_REFRESH, NULL, NULL );
  gtk_menu_item_set_submenu ( GTK_MENU_ITEM(items), GTK_WIDGET(submenu_sort) );

  (void)vu_menu_add_item ( submenu_sort, _("Name _Ascending"), GTK_STOCK_SORT_ASCENDING, G_CALLBACK(aggregate_layer_sort_a2z), values );
  (void)vu_menu_add_item ( submenu_sort, _("Name _Descending"), GTK_STOCK_SORT_DESCENDING, G_CALLBACK(aggregate_layer_sort_z2a), values );
  (void)vu_menu_add_item ( submenu_sort, _("Date Ascending"), GTK_STOCK_SORT_ASCENDING, G_CALLBACK(aggregate_layer_sort_timestamp_ascend), values );
  (void)vu_menu_add_item ( submenu_sort, _("Date Descending"), GTK_STOCK_SORT_DESCENDING, G_CALLBACK(aggregate_layer_sort_timestamp_descend), values );

  (void)vu_menu_add_item ( menu, _("_Statistics"), NULL, G_CALLBACK(aggregate_layer_analyse), values );
  (void)vu_menu_add_item ( menu, _("Track _List..."), GTK_STOCK_INDEX, G_CALLBACK(aggregate_layer_track_list_dialog), values );
  (void)vu_menu_add_item ( menu, _("_Waypoint List..."), GTK_STOCK_INDEX, G_CALLBACK(aggregate_layer_waypoint_list_dialog), values );

  GtkMenu *search_submenu = GTK_MENU(gtk_menu_new());
  GtkWidget *itemsr = vu_menu_add_item ( menu, _("Searc_h"), GTK_STOCK_JUMP_TO, NULL, NULL );
  gtk_menu_item_set_submenu ( GTK_MENU_ITEM(itemsr), GTK_WIDGET(search_submenu) );

  GtkWidget *itemd = vu_menu_add_item ( search_submenu, _("By _Date..."), NULL, G_CALLBACK(aggregate_layer_search_date), values );
  gtk_widget_set_tooltip_text ( itemd, _("Find the first item with a specified date") );

  (void)vu_menu_add_item ( menu, _("Load E_xternal Layers"), NULL, G_CALLBACK(aggregate_layer_load_external_layers_click), values );

  (void)vu_menu_add_item ( menu, _("Append File..."), GTK_STOCK_ADD, G_CALLBACK(aggregate_layer_file_load), values );
}

static void disconnect_layer_signal ( VikLayer *vl, VikAggregateLayer *val )
{
  guint number_handlers = g_signal_handlers_disconnect_matched(vl, G_SIGNAL_MATCH_DATA, 0, 0, 0, 0, val);
  if ( number_handlers != 1 )
    g_critical ("%s: Unexpected number of disconnect handlers: %d", __FUNCTION__, number_handlers);
}

void vik_aggregate_layer_free ( VikAggregateLayer *val )
{
  g_list_foreach ( val->children, (GFunc)(disconnect_layer_signal), val );
  g_list_foreach ( val->children, (GFunc)(g_object_unref), NULL );
  g_list_free ( val->children );
  if ( val->tracks_analysis_dialog != NULL )
    gtk_widget_destroy ( val->tracks_analysis_dialog );
}

static void delete_layer_iter ( VikLayer *vl )
{
  if ( vl->realized )
    vik_treeview_item_delete ( vl->vt, &(vl->iter) );
}

void vik_aggregate_layer_clear ( VikAggregateLayer *val )
{
  g_list_foreach ( val->children, (GFunc)(disconnect_layer_signal), val );
  g_list_foreach ( val->children, (GFunc)(delete_layer_iter), NULL );
  g_list_foreach ( val->children, (GFunc)(g_object_unref), NULL );
  g_list_free ( val->children );
  val->children = NULL;
}

static void aggregate_layer_delete_common ( VikAggregateLayer *val, VikLayer *vl )
{
  val->children = g_list_remove ( val->children, vl );
  disconnect_layer_signal ( vl, val );
  g_object_unref ( vl );
}

gboolean vik_aggregate_layer_delete ( VikAggregateLayer *val, GtkTreeIter *iter )
{
  VikLayer *l = VIK_LAYER( vik_treeview_item_get_pointer ( VIK_LAYER(val)->vt, iter ) );
  gboolean was_visible = l->visible;

  vik_treeview_item_delete ( VIK_LAYER(val)->vt, iter );
  aggregate_layer_delete_common ( val, l );

  return was_visible;
}

/**
 * Delete a child layer from the aggregate layer
 */
gboolean vik_aggregate_layer_delete_layer ( VikAggregateLayer *val, VikLayer *vl )
{
  gboolean was_visible = vl->visible;

  if ( vl->realized && &vl->iter )
    vik_treeview_item_delete ( VIK_LAYER(val)->vt, &vl->iter );
  aggregate_layer_delete_common ( val, vl );

  return was_visible;
}

#if 0
/* returns 0 == we're good, 1 == didn't find any layers, 2 == got rejected */
guint vik_aggregate_layer_tool ( VikAggregateLayer *val, VikLayerTypeEnum layer_type, VikToolInterfaceFunc tool_func, GdkEventButton *event, VikViewport *vvp )
{
  GList *iter = val->children;
  gboolean found_rej = FALSE;
  if (!iter)
    return FALSE;
  while (iter->next)
    iter = iter->next;

  while ( iter )
  {
    /* if this layer "accepts" the tool call */
    if ( VIK_LAYER(iter->data)->visible && VIK_LAYER(iter->data)->type == layer_type )
    {
      if ( tool_func ( VIK_LAYER(iter->data), event, vvp ) )
        return 0;
      else
        found_rej = TRUE;
    }

    /* recursive -- try the same for the child aggregate layer. */
    else if ( VIK_LAYER(iter->data)->visible && VIK_LAYER(iter->data)->type == VIK_LAYER_AGGREGATE )
    {
      gint rv = vik_aggregate_layer_tool(VIK_AGGREGATE_LAYER(iter->data), layer_type, tool_func, event, vvp);
      if ( rv == 0 )
        return 0;
      else if ( rv == 2 )
        found_rej = TRUE;
    }
    iter = iter->prev;
  }
  return found_rej ? 2 : 1; /* no one wanted to accept the tool call in this layer */
}
#endif 

VikLayer *vik_aggregate_layer_get_top_visible_layer_of_type ( VikAggregateLayer *val, VikLayerTypeEnum type )
{
  VikLayer *rv;
  GList *ls = val->children;
  if (!ls)
    return NULL;
  while (ls->next)
    ls = ls->next;

  while ( ls )
  {
    VikLayer *vl = VIK_LAYER(ls->data);
    if ( vl->visible && vl->type == type )
      return vl;
    else if ( vl->visible && vl->type == VIK_LAYER_AGGREGATE )
    {
      rv = vik_aggregate_layer_get_top_visible_layer_of_type(VIK_AGGREGATE_LAYER(vl), type);
      if ( rv )
        return rv;
    }
    ls = ls->prev;
  }
  return NULL;
}

GList *vik_aggregate_layer_get_all_layers_of_type(VikAggregateLayer *val, GList *layers, VikLayerTypeEnum type, gboolean include_invisible)
{
  GList *l = layers;
  GList *children = val->children;
  VikLayer *vl;
  if (!children)
    return layers;

  // Where appropriate *don't* include non-visible layers
  while (children) {
    vl = VIK_LAYER(children->data);
    if (vl->type == VIK_LAYER_AGGREGATE ) {
      // Don't even consider invisible aggregates, unless told to
      if (vl->visible || include_invisible)
        l = vik_aggregate_layer_get_all_layers_of_type(VIK_AGGREGATE_LAYER(children->data), l, type, include_invisible);
    }
    else if (vl->type == type) {
      if (vl->visible || include_invisible)
        l = g_list_prepend(l, children->data); /* now in top down order */
    }
    else if (type == VIK_LAYER_TRW) {
      /* GPS layers contain TRW layers. cf with usage in file.c */
      if (VIK_LAYER(children->data)->type == VIK_LAYER_GPS) {
	if (VIK_LAYER(children->data)->visible || include_invisible) {
	  if (!vik_gps_layer_is_empty(VIK_GPS_LAYER(children->data))) {
	    /*
	      can not use g_list_concat due to wrong copy method - crashes if used a couple times !!
	      l = g_list_concat (l, vik_gps_layer_get_children (VIK_GPS_LAYER(children->data)));
	    */
	    /* create own copy method instead :( */
	    GList *gps_trw_layers = (GList *)vik_gps_layer_get_children (VIK_GPS_LAYER(children->data));
	    int n_layers = g_list_length (gps_trw_layers);
	    int layer = 0;
	    for ( layer = 0; layer < n_layers; layer++) {
	      l = g_list_prepend(l, gps_trw_layers->data);
	      gps_trw_layers = gps_trw_layers->next;
	    }
	    g_list_free(gps_trw_layers);
	  }
	}
      }
    }
    children = children->next;
  }
  return l;
}

void vik_aggregate_layer_realize ( VikAggregateLayer *val, VikTreeview *vt, GtkTreeIter *layer_iter )
{
  GList *i = val->children;
  GtkTreeIter iter;
  VikLayer *vl = VIK_LAYER(val);
  VikLayer *vli;
  while ( i )
  {
    vli = VIK_LAYER(i->data);
    vik_treeview_add_layer ( vl->vt, layer_iter, &iter, vli->name, val, TRUE,
                             vli, vli->type, vli->type, vik_layer_get_timestamp(vli) );
    if ( ! vli->visible )
      vik_treeview_item_set_visible ( vl->vt, &iter, FALSE );
    vik_layer_realize ( vli, vl->vt, &iter );
    i = i->next;
  }
}

const GList *vik_aggregate_layer_get_children ( VikAggregateLayer *val )
{
  return val->children;
}

gboolean vik_aggregate_layer_is_empty ( VikAggregateLayer *val )
{
  if ( val->children )
    return FALSE;
  return TRUE;
}

static void aggregate_layer_drag_drop_request ( VikAggregateLayer *val_src, VikAggregateLayer *val_dest, GtkTreeIter *src_item_iter, GtkTreePath *dest_path )
{
  VikTreeview *vt = VIK_LAYER(val_src)->vt;
  VikLayer *vl = vik_treeview_item_get_pointer(vt, src_item_iter);
  GtkTreeIter dest_iter;
  gchar *dp;
  gboolean target_exists;

  dp = gtk_tree_path_to_string(dest_path);
  target_exists = vik_treeview_get_iter_from_path_str(vt, &dest_iter, dp);

  /* vik_aggregate_layer_delete unrefs, but we don't want that here.
   * we're still using the layer. */
  g_object_ref ( vl );
  vik_aggregate_layer_delete(val_src, src_item_iter);

  if (target_exists) {
    vik_aggregate_layer_insert_layer(val_dest, vl, &dest_iter);
  } else {
    vik_aggregate_layer_insert_layer(val_dest, vl, NULL); /* append */
  }
  g_free(dp);
}

/**
 * Generate tooltip text for the layer.
 */
static const gchar* aggregate_layer_tooltip ( VikAggregateLayer *val )
{
  static gchar tmp_buf[128];
  tmp_buf[0] = '\0';

  GList *children = val->children;
  if ( children ) {
    gint nn = g_list_length (children);
    // Could have a more complicated tooltip that numbers each type of layers,
    //  but for now a simple overall count
    g_snprintf (tmp_buf, sizeof(tmp_buf), ngettext("One layer", "%d layers", nn), nn );
  }
  else
    g_snprintf (tmp_buf, sizeof(tmp_buf), _("Empty") );
  return tmp_buf;
}

/**
 * Return number of layers held
 */
guint vik_aggregate_layer_count ( VikAggregateLayer *val )
{
  guint nn = 0;
  GList *children = val->children;
  if ( children ) {
    nn = g_list_length (children);
  }
  return nn;
}
