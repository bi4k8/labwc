// SPDX-License-Identifier: GPL-2.0-only
#include "labwc.h"
#include "view.h"

static int
max_move_scale(double pos_cursor, double pos_current,
	double size_current, double size_orig)
{
	double anchor_frac = (pos_cursor - pos_current) / size_current;
	int pos_new = pos_cursor - (size_orig * anchor_frac);
	if (pos_new < pos_current) {
		/* Clamp by using the old offsets of the maximized window */
		pos_new = pos_current;
	}
	return pos_new;
}

void
interactive_begin(struct view *view, enum input_mode mode, uint32_t edges)
{
	/*
	 * This function sets up an interactive move or resize operation, where
	 * the compositor stops propagating pointer events to clients and
	 * instead consumes them itself, to move or resize windows.
	 */
	struct server *server = view->server;
	struct seat *seat = &server->seat;
	struct wlr_box geometry = {
		.x = view->x,
		.y = view->y,
		.width = view->w,
		.height = view->h
	};

	switch (mode) {
	case LAB_INPUT_STATE_MOVE:
		if (view->fullscreen) {
			/**
			 * We don't allow moving fullscreen windows.
			 *
			 * If you think there is a good reason to allow
			 * it, feel free to open an issue explaining
			 * your use-case.
			 */
			return;
		}
		if (view->maximized || view->tiled) {
			/*
			 * Un-maximize and restore natural width/height.
			 * Don't reset tiled state yet since we may want
			 * to keep it (in the snap-to-maximize case).
			 */
			geometry = view->natural_geometry;
			geometry.x = max_move_scale(seat->cursor->x, view->x,
				view->w, geometry.width);
			geometry.y = max_move_scale(seat->cursor->y, view->y,
				view->h, geometry.height);
			view_restore_to(view, geometry);
		} else {
			/* Store natural geometry at start of move */
			view_store_natural_geometry(view);
		}
		cursor_set(seat, LAB_CURSOR_GRAB);
		break;
	case LAB_INPUT_STATE_RESIZE:
		if (view->maximized || view->fullscreen) {
			/*
			 * We don't allow resizing while maximized or
			 * fullscreen.
			 */
			return;
		}
		/*
		 * Reset tiled state but keep the same geometry as the
		 * starting point for the resize.
		 */
		view_set_untiled(view);
		cursor_set(seat, cursor_get_from_edge(edges));
		break;
	default:
		/* Should not be reached */
		return;
	}

	server->input_mode = mode;
	server->grabbed_view = view;
	/* Remember view and cursor positions at start of move/resize */
	server->grab_x = seat->cursor->x;
	server->grab_y = seat->cursor->y;
	server->grab_box = geometry;
	server->resize_edges = edges;
}

/* Returns true if view was snapped to any edge */
static bool
snap_to_edge(struct view *view)
{
	int snap_range = rc.snap_edge_range;
	if (!snap_range) {
		return false;
	}

	/* Translate into output local coordinates */
	double cursor_x = view->server->seat.cursor->x;
	double cursor_y = view->server->seat.cursor->y;
	wlr_output_layout_output_coords(view->server->output_layout,
		view->output->wlr_output, &cursor_x, &cursor_y);

	/*
	 * Don't store natural geometry here (it was
	 * stored already in interactive_begin())
	 */
	struct wlr_box *area = &view->output->usable_area;
	if (cursor_x <= area->x + snap_range) {
		view_snap_to_edge(view, "left",
			/*store_natural_geometry*/ false);
	} else if (cursor_x >= area->x + area->width - snap_range) {
		view_snap_to_edge(view, "right",
			/*store_natural_geometry*/ false);
	} else if (cursor_y <= area->y + snap_range) {
		if (rc.snap_top_maximize) {
			view_maximize(view, true,
				/*store_natural_geometry*/ false);
		} else {
			view_snap_to_edge(view, "up",
				/*store_natural_geometry*/ false);
		}
	} else if (cursor_y >= area->y + area->height - snap_range) {
		view_snap_to_edge(view, "down",
			/*store_natural_geometry*/ false);
	} else {
		/* Not close to any edge */
		return false;
	}

	return true;
}

void
interactive_finish(struct view *view)
{
	if (view->server->grabbed_view == view) {
		enum input_mode mode = view->server->input_mode;
		view->server->input_mode = LAB_INPUT_STATE_PASSTHROUGH;
		view->server->grabbed_view = NULL;
		if (mode == LAB_INPUT_STATE_MOVE) {
			if (!snap_to_edge(view)) {
				/* Reset tiled state if not snapped */
				view_set_untiled(view);
			}
		}
		/* Update focus/cursor image */
		cursor_update_focus(view->server);
	}
}

/*
 * Cancels interative move/resize without changing the state of the of
 * the view in any way. This may leave the tiled state inconsistent with
 * the actual geometry of the view.
 */
void
interactive_cancel(struct view *view)
{
	if (view->server->grabbed_view == view) {
		view->server->input_mode = LAB_INPUT_STATE_PASSTHROUGH;
		view->server->grabbed_view = NULL;
		/* Update focus/cursor image */
		cursor_update_focus(view->server);
	}
}
