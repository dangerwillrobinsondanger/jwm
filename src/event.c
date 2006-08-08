/****************************************************************************
 * Functions to handle XServer events.
 * Copyright (C) 2004 Joe Wingbermuehle
 ****************************************************************************/

#include "jwm.h"
#include "event.h"
#include "client.h"
#include "main.h"
#include "hint.h"
#include "tray.h"
#include "pager.h"
#include "desktop.h"
#include "cursor.h"
#include "icon.h"
#include "taskbar.h"
#include "confirm.h"
#include "swallow.h"
#include "popup.h"
#include "winmenu.h"
#include "root.h"
#include "move.h"
#include "resize.h"
#include "key.h"
#include "clock.h"
#include "place.h"
#include "dock.h"
#include "timing.h"
#include "traybutton.h"

#define MIN_TIME_DELTA 50

static void Signal();
static void DispatchBorderButtonEvent(const XButtonEvent *event,
	ClientNode *np);

static void HandleConfigureRequest(const XConfigureRequestEvent *event);
static int HandleExpose(const XExposeEvent *event);
static int HandlePropertyNotify(const XPropertyEvent *event);
static void HandleClientMessage(const XClientMessageEvent *event);
static void HandleColormapChange(const XColormapEvent *event);
static int HandleDestroyNotify(const XDestroyWindowEvent *event);
static void HandleMapRequest(const XMapEvent *event);
static void HandleUnmapNotify(const XUnmapEvent *event);
static void HandleButtonEvent(const XButtonEvent *event);
static void HandleKeyPress(const XKeyEvent *event);
static void HandleEnterNotify(const XCrossingEvent *event);
static void HandleLeaveNotify(const XCrossingEvent *event);
static void HandleMotionNotify(const XMotionEvent *event);
static int HandleSelectionClear(const XSelectionClearEvent *event);

static void HandleNetMoveResize(const XClientMessageEvent *event,
	ClientNode *np);
static void HandleNetWMState(const XClientMessageEvent *event,
	ClientNode *np);

#ifdef USE_SHAPE
static void HandleShapeEvent(const XShapeEvent *event);
#endif

/****************************************************************************
 ****************************************************************************/
void WaitForEvent(XEvent *event) {

	struct timeval timeout;
	fd_set fds;
	int fd;
	int handled;

	fd = JXConnectionNumber(display);

	do {

		while(JXPending(display) == 0) {
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			timeout.tv_usec = 0;
			timeout.tv_sec = 1;
			if(select(fd + 1, &fds, NULL, NULL, &timeout) <= 0) {
				Signal();
			}
		}

		Signal();

		JXNextEvent(display, event);

		switch(event->type) {
		case ConfigureRequest:
			HandleConfigureRequest(&event->xconfigurerequest);
			handled = 1;
			break;
		case MapRequest:
			HandleMapRequest(&event->xmap);
			handled = 1;
			break;
		case PropertyNotify:
			handled = HandlePropertyNotify(&event->xproperty);
			break;
		case ClientMessage:
			HandleClientMessage(&event->xclient);
			handled = 1;
			break;
		case UnmapNotify:
			HandleUnmapNotify(&event->xunmap);
			handled = 1;
			break;
		case Expose:
			handled = HandleExpose(&event->xexpose);
			break;
		case ColormapNotify:
			HandleColormapChange(&event->xcolormap);
			handled = 1;
			break;
		case DestroyNotify:
			handled = HandleDestroyNotify(&event->xdestroywindow);
			break;
		case SelectionClear:
			handled = HandleSelectionClear(&event->xselectionclear);
			break;
		case ResizeRequest:
			handled = HandleDockResizeRequest(&event->xresizerequest);
			break;
		case MotionNotify:
			SetMousePosition(event->xmotion.x_root, event->xmotion.y_root);
			handled = 0;
			break;
		case ConfigureNotify:
		case CreateNotify:
		case MapNotify:
		case ReparentNotify:
		case GraphicsExpose:
		case NoExpose:
			handled = 1;
			break;
		default:
#ifdef USE_SHAPE
			if(haveShape && event->type == shapeEvent) {
				HandleShapeEvent((XShapeEvent*)event);
				handled = 1;
			} else {
				handled = 0;
			}
#else
			handled = 0;
#endif
			break;
		}

		if(!handled) {
			handled = ProcessTrayEvent(event);
		}
		if(!handled) {
			handled = ProcessDialogEvent(event);
		}
		if(!handled) {
			handled = ProcessSwallowEvent(event);
		}
		if(!handled) {
			handled = ProcessPopupEvent(event);
		}

	} while(handled && !shouldExit);

}

/****************************************************************************
 ****************************************************************************/
void Signal() {

	static TimeType last = ZERO_TIME;

	TimeType now;
	int x, y;

	GetCurrentTime(&now);

	if(GetTimeDifference(&now, &last) < MIN_TIME_DELTA) {
		return;
	}
	last = now;

	GetMousePosition(&x, &y);

	SignalTaskbar(&now, x, y);
	SignalTrayButton(&now, x, y);
	SignalClock(&now, x, y);
	SignalTray(&now, x, y);
	SignalPopup(&now, x, y);

}

/****************************************************************************
 ****************************************************************************/
void ProcessEvent(XEvent *event) {

	switch(event->type) {
	case ButtonPress:
	case ButtonRelease:
		HandleButtonEvent(&event->xbutton);
		break;
	case KeyPress:
		HandleKeyPress(&event->xkey);
		break;
	case EnterNotify:
		HandleEnterNotify(&event->xcrossing);
		break;
	case LeaveNotify:
		HandleLeaveNotify(&event->xcrossing);
		break;
	case MotionNotify:
		while(JXCheckTypedEvent(display, MotionNotify, event));
		HandleMotionNotify(&event->xmotion);
		break;
	case DestroyNotify:
	case Expose:
	case KeyRelease:
	case ConfigureNotify:
		break;
	default:
		Debug("Unknown event type: %d", event->type);
		break;
	}
}

/****************************************************************************
 ****************************************************************************/
void DiscardMotionEvents(XEvent *event, Window w) {

	XEvent temp;

	while(JXCheckTypedEvent(display, MotionNotify, &temp)) {
		SetMousePosition(temp.xmotion.x_root, temp.xmotion.y_root);
		if(temp.xmotion.window == w) {
			*event = temp;
		}
	}

}

/****************************************************************************
 ****************************************************************************/
int HandleSelectionClear(const XSelectionClearEvent *event) {

	return HandleDockSelectionClear(event);

}

/****************************************************************************
 ****************************************************************************/
void HandleButtonEvent(const XButtonEvent *event) {
	int x, y;
	ClientNode *np;

	np = FindClientByParent(event->window);
	if(np) {
		RaiseClient(np);
		if(focusModel == FOCUS_CLICK) {
			FocusClient(np);
		}
		switch(event->button) {
		case Button1:
			DispatchBorderButtonEvent(event, np);
			break;
		case Button2:
			MoveClient(np, event->x, event->y);
			break;
		case Button3:
			x = event->x + np->x;
			y = event->y + np->y;
			if(np->state.border & BORDER_OUTLINE) {
				x -= borderWidth;
				y -= borderWidth;
			}
			if(np->state.border & BORDER_TITLE) {
				y -= titleHeight;
			}
			ShowWindowMenu(np, x, y);
			break;
		case Button4:
			ShadeClient(np);
			break;
		case Button5:
			UnshadeClient(np);
			break;
		default:
			break;
		}
	} else if(event->window == rootWindow && event->type == ButtonPress) {
		if(!ShowRootMenu(event->button, event->x, event->y)) {
			if(event->button == 4) {
				PreviousDesktop();
			} else if(event->button == 5) {
				NextDesktop();
			}
		}
	} else {
		np = FindClientByWindow(event->window);
		if(np) {
			switch(event->button) {
			case Button1:
			case Button2:
			case Button3:
				RaiseClient(np);
				if(focusModel == FOCUS_CLICK) {
					FocusClient(np);
				}
				if(event->state & Mod1Mask) {
					MoveClient(np, event->x, event->y);
				}
				break;
			default:
				break;
			}
			JXAllowEvents(display, ReplayPointer, CurrentTime);
		}
	}

	UpdatePager();
}

/****************************************************************************
 ****************************************************************************/
void HandleKeyPress(const XKeyEvent *event) {
	ClientNode *np;
	KeyType key;

	key = GetKey(event);

	np = GetActiveClient();

	switch(key & 0xFF) {
	case KEY_EXEC:
		RunKeyCommand(event);
		break;
	case KEY_DESKTOP:
		if(key >> 8) {
			ChangeDesktop((key >> 8) - 1);
		} else {
			NextDesktop();
		}
		break;
	case KEY_NEXT:
		FocusNext();
		break;
	case KEY_NEXT_STACKED:
		FocusNextStackedCircular();
		break;
	case KEY_CLOSE:
		if(np) {
			DeleteClient(np);
		}
		break;
	case KEY_SHADE:
		if(np) {
			if(np->state.status & STAT_SHADED) {
				UnshadeClient(np);
			} else {
				ShadeClient(np);
			}
		}
		break;
	case KEY_MOVE:
		if(np) {
			MoveClientKeyboard(np);
		}
		break;
	case KEY_RESIZE:
		if(np) {
			ResizeClientKeyboard(np);
		}
		break;
	case KEY_MIN:
		if(np) {
			MinimizeClient(np);
		}
		break;
	case KEY_MAX:
		if(np) {
			MaximizeClient(np);
		}
		break;
	case KEY_ROOT:
		ShowRootMenu(1, 0, 0);
		break;
	case KEY_WIN:
		if(np) {
			ShowWindowMenu(np, np->x, np->y);
		}
		break;
	case KEY_RESTART:
		Restart();
		break;
	case KEY_EXIT:
		Exit();
		break;
	default:
		break;
	}

}

/****************************************************************************
 ****************************************************************************/
void HandleConfigureRequest(const XConfigureRequestEvent *event) {

	XWindowChanges wc;
	ClientNode *np;
	int north, south, east, west;
	int changed;

	np = FindClientByWindow(event->window);
	if(np && np->window == event->window) {

		if(np->controller) {
			(np->controller)(0);
		}

		changed = 0;
		if((event->value_mask & CWWidth) && (event->width != np->width)) {
			np->width = event->width;
			changed = 1;
		}
		if((event->value_mask & CWHeight) && (event->height != np->height)) {
			np->height = event->height;
			changed = 1;
		}
		if((event->value_mask & CWX) && (event->x != np->x)) {
			np->x = event->x;
			changed = 1;
		}
		if((event->value_mask & CWY) && (event->y != np->y)) {
			np->y = event->y;
			changed = 1;
		}

		if(!changed) {
			return;
		}

		GetBorderSize(np, &north, &south, &east, &west);

		wc.stack_mode = Above;
		wc.sibling = np->parent;
		wc.border_width = 0;

		ConstrainSize(np);

		wc.x = np->x;
		wc.y = np->y;
		wc.width = np->width + east + west;
		wc.height = np->height + north + south;
		JXConfigureWindow(display, np->parent, event->value_mask, &wc);

		wc.x = west;
		wc.y = north;
		wc.width = np->width;
		wc.height = np->height;
		JXConfigureWindow(display, np->window, event->value_mask, &wc);

	} else {

		wc.stack_mode = event->detail;
		wc.sibling = event->above;
		wc.border_width = event->border_width;
		wc.x = event->x;
		wc.y = event->y;
		wc.width = event->width > rootWidth ? rootWidth : event->width;
		wc.height = event->height > rootHeight ? rootHeight : event->height;
		JXConfigureWindow(display, event->window, event->value_mask, &wc);

	}

}

/****************************************************************************
 ****************************************************************************/
void HandleEnterNotify(const XCrossingEvent *event) {

	ClientNode *np;
	Cursor cur;

	SetMousePosition(event->x_root, event->y_root);

	np = FindClientByWindow(event->window);
	if(np) {
		if(!(np->state.status & STAT_ACTIVE) && (focusModel == FOCUS_SLOPPY)) {
			FocusClient(np);
		}
		if(np->parent == event->window) {
			np->borderAction = GetBorderActionType(np, event->x, event->y);
			cur = GetFrameCursor(np->borderAction);
			JXDefineCursor(display, np->parent, cur);
		} else if(np->borderAction != BA_NONE) {
			SetDefaultCursor(np->parent);
			np->borderAction = BA_NONE;
		}
	}

}

/****************************************************************************
 ****************************************************************************/
void HandleLeaveNotify(const XCrossingEvent *event) {

	ClientNode *np;

	SetMousePosition(event->x_root, event->y_root);

	np = FindClientByParent(event->window);
	if(np) {
		SetDefaultCursor(np->parent);
	}

}

/****************************************************************************
 ****************************************************************************/
int HandleExpose(const XExposeEvent *event) {

	ClientNode *np;

	np = FindClientByWindow(event->window);
	if(np) {
		if(event->window == np->parent) {
			DrawBorder(np, event);
			return 1;
		} else if(event->window == np->window
			&& np->state.status & STAT_WMDIALOG) {
			return 0;
		} else {
			return 1;
		}
	} else {
		return event->count ? 1 : 0;
	}

}

/****************************************************************************
 ****************************************************************************/
int HandlePropertyNotify(const XPropertyEvent *event) {

	ClientNode *np;
	int changed;

	np = FindClientByWindow(event->window);
	if(np) {
		changed = 0;
		switch(event->atom) {
		case XA_WM_NAME:
			ReadWMName(np);
			changed = 1;
			break;
		case XA_WM_NORMAL_HINTS:
			ReadWMNormalHints(np);
			changed = 1;
			break;
		case XA_WM_HINTS:
		case XA_WM_ICON_NAME:
		case XA_WM_CLIENT_MACHINE:
			break;
		default:
			if(event->atom == atoms[ATOM_WM_COLORMAP_WINDOWS]) {
				ReadWMColormaps(np);
				UpdateClientColormap(np);
			} else if(event->atom == atoms[ATOM_NET_WM_ICON]) {
				LoadIcon(np);
				changed = 1;
			} else if(event->atom == atoms[ATOM_NET_WM_NAME]) {
				ReadWMName(np);
				changed = 1;
			} else if(event->atom == atoms[ATOM_NET_WM_STRUT_PARTIAL]) {
				ReadClientStrut(np);
			} else if(event->atom == atoms[ATOM_NET_WM_STRUT]) {
				ReadClientStrut(np);
			}
			break;
		}

		if(changed) {
			DrawBorder(np, NULL);
			UpdateTaskBar();
			UpdatePager();
		}
		if(np->state.status & STAT_WMDIALOG) {
			return 0;
		} else {
			return 1;
		}
	}

	return 1;
}

/****************************************************************************
 ****************************************************************************/
void HandleClientMessage(const XClientMessageEvent *event) {

	ClientNode *np;
	long mask, flags;
#ifdef DEBUG
	char *atomName;
#endif

	np = FindClientByWindow(event->window);
	if(np) {
		if(event->message_type == atoms[ATOM_WIN_STATE]) {

			mask = event->data.l[0];
			flags = event->data.l[1];

			if(mask & WIN_STATE_STICKY) {
				if(flags & WIN_STATE_STICKY) {
					SetClientSticky(np, 1);
				} else {
					SetClientSticky(np, 0);
				}
			}

			if(mask & WIN_STATE_HIDDEN) {
				if(flags & WIN_STATE_HIDDEN) {
					np->state.status |= STAT_NOLIST;
				} else {
					np->state.status &= ~STAT_NOLIST;
				}
				UpdateTaskBar();
				UpdatePager();
			}

		} else if(event->message_type == atoms[ATOM_WIN_LAYER]) {

			SetClientLayer(np, event->data.l[0]);

		} else if(event->message_type == atoms[ATOM_WM_CHANGE_STATE]) {

			if(np->controller) {
				(np->controller)(0);
			}

			switch(event->data.l[0]) {
			case WithdrawnState:
				SetClientWithdrawn(np);
				break;
			case IconicState:
				MinimizeClient(np);
				break;
			case NormalState:
				RestoreClient(np, 1);
				break;
			default:
				break;
			}

		} else if(event->message_type == atoms[ATOM_NET_ACTIVE_WINDOW]) {

			RestoreClient(np, 1);
			FocusClient(np);

		} else if(event->message_type == atoms[ATOM_NET_WM_DESKTOP]) {

			if(event->data.l[0] == ~0L) {
				SetClientSticky(np, 1);
			} else {

				if(np->controller) {
					(np->controller)(0);
				}

				if(event->data.l[0] >= 0 && event->data.l[0] < (long)desktopCount) {
					np->state.status &= ~STAT_STICKY;
					SetClientDesktop(np, event->data.l[0]);
				}
			}

		} else if(event->message_type == atoms[ATOM_NET_CLOSE_WINDOW]) {

			DeleteClient(np);

		} else if(event->message_type == atoms[ATOM_NET_MOVERESIZE_WINDOW]) {

			HandleNetMoveResize(event, np);

		} else if(event->message_type == atoms[ATOM_NET_WM_STATE]) {

			HandleNetWMState(event, np);

		} else {

#ifdef DEBUG
			atomName = JXGetAtomName(display, event->message_type);
			Debug("Uknown ClientMessage to client: %s", atomName);
			JXFree(atomName);
#endif

		}

	} else if(event->window == rootWindow) {

		if(event->message_type == atoms[ATOM_JWM_RESTART]) {
			Restart();
		} else if(event->message_type == atoms[ATOM_JWM_EXIT]) {
			Exit();
		} else if(event->message_type == atoms[ATOM_NET_CURRENT_DESKTOP]) {
			ChangeDesktop(event->data.l[0]);
		} else {
#ifdef DEBUG
			atomName = JXGetAtomName(display, event->message_type);
			Debug("Uknown ClientMessage to root: %s", atomName);
			JXFree(atomName);
#endif
		}

	} else if(event->message_type == atoms[ATOM_NET_SYSTEM_TRAY_OPCODE]) {

		HandleDockEvent(event);

	}

}

/****************************************************************************
 * Handle a _NET_MOVERESIZE_WINDOW request.
 ****************************************************************************/
void HandleNetMoveResize(const XClientMessageEvent *event, ClientNode *np) {

	long flags, gravity;
	long x, y;
	long width, height;
	int deltax, deltay;
	int north, south, east, west;

	Assert(event);
	Assert(np);

	gravity = event->data.l[0] & 0xFF;
	flags = event->data.l[0] >> 8;

	x = np->x;
	y = np->y;
	width = np->width;
	height = np->height;

	if(flags & (1 << 0)) {
		x = event->data.l[1];
	}
	if(flags & (1 << 1)) {
		y = event->data.l[2];
	}
	if(flags & (1 << 2)) {
		width = event->data.l[3];
	}
	if(flags & (1 << 3)) {
		height = event->data.l[4];
	}

	if(gravity == 0) {
		gravity = np->gravity;
	}

	GetBorderSize(np, &north, &south, &east, &west);
	GetGravityDelta(np, &deltax, &deltay);

	x -= deltax;
	y -= deltay;

	np->x = x;
	np->y = y;
	np->width = width;
	np->height = height;

	JXMoveResizeWindow(display, np->parent,
		np->x - west, np->y - north,
		np->width + east + west,
		np->height + north + south);
	JXMoveResizeWindow(display, np->window, west, north,
		np->width, np->height);

	WriteState(np);
	SendConfigureEvent(np);

}

/****************************************************************************
 * Handle a _NET_WM_STATE request.
 ****************************************************************************/
void HandleNetWMState(const XClientMessageEvent *event, ClientNode *np) {

	int actionMaximize;
	int actionStick;
	int actionShade;
	int x;

	/* Up to two actions to be applied together, figure it out. */
	actionMaximize = 0;
	actionStick = 0;
	actionShade = 0;

	for(x = 1; x <= 2; x++) {
		if(event->data.l[x]
			== (long)atoms[ATOM_NET_WM_STATE_STICKY]) {
			actionStick = 1;
		} else if(event->data.l[x]
			== (long)atoms[ATOM_NET_WM_STATE_MAXIMIZED_VERT]) {
			actionMaximize = 1;
		} else if(event->data.l[x]
			== (long)atoms[ATOM_NET_WM_STATE_MAXIMIZED_HORZ]) {
			actionMaximize = 1;
		} else if(event->data.l[x]
			== (long)atoms[ATOM_NET_WM_STATE_SHADED]) {
			actionShade = 1;
		}
	}

	switch(event->data.l[0]) {
	case 0: /* Remove */
		if(actionStick) {
			SetClientSticky(np, 0);
		}
		if(actionMaximize && (np->state.status & STAT_MAXIMIZED)) {
			MaximizeClient(np);
		}
		if(actionShade) {
			UnshadeClient(np);
		}
		break;
	case 1: /* Add */
		if(actionStick) {
			SetClientSticky(np, 1);
		}
		if(actionMaximize && !(np->state.status & STAT_MAXIMIZED)) {
			MaximizeClient(np);
		}
		if(actionShade) {
			ShadeClient(np);
		}
		break;
	case 2: /* Toggle */
		if(actionStick) {
			if(np->state.status & STAT_STICKY) {
				SetClientSticky(np, 0);
			} else {
				SetClientSticky(np, 1);
			}
		}
		if(actionMaximize) {
			MaximizeClient(np);
		}
		if(actionShade) {
			if(np->state.status & STAT_SHADED) {
				UnshadeClient(np);
			} else {
				ShadeClient(np);
			}
		}
		break;
	default:
		Debug("bad _NET_WM_STATE action: %ld", event->data.l[0]);
		break;
	}
}

/****************************************************************************
 ****************************************************************************/
void HandleMotionNotify(const XMotionEvent *event) {

	ClientNode *np;
	Cursor cur;
	BorderActionType action;

	if(event->is_hint) {
		return;
	}

	SetMousePosition(event->x_root, event->y_root);

	np = FindClientByParent(event->window);
	if(np && (np->state.border & BORDER_OUTLINE)) {
		action = GetBorderActionType(np, event->x, event->y);
		if(np->borderAction != action) {
			np->borderAction = action;
			cur = GetFrameCursor(action);
			JXDefineCursor(display, np->parent, cur);
		}
	}

}

/****************************************************************************
 ****************************************************************************/
#ifdef USE_SHAPE
void HandleShapeEvent(const XShapeEvent *event) {

	ClientNode *np;

	np = FindClientByWindow(event->window);
	if(np) {
		SetShape(np);
	}

}
#endif /* USE_SHAPE */

/****************************************************************************
 ****************************************************************************/
void HandleColormapChange(const XColormapEvent *event) {
	ClientNode *np;

	if(event->new == True) {
		np = FindClientByWindow(event->window);
		if(np) {
			np->cmap = event->colormap;
			UpdateClientColormap(np);
		}
	}

}

/****************************************************************************
 ****************************************************************************/
void HandleMapRequest(const XMapEvent *event) {

	ClientNode *np;

	Assert(event);

	if(CheckSwallowMap(event)) {
		return;
	}

	np = FindClientByWindow(event->window);
	if(!np) {
		JXSync(display, False);
		JXGrabServer(display);
		np = AddClientWindow(event->window, 0, 1);
		if(np) {
			if(focusModel == FOCUS_CLICK) {
				FocusClient(np);
			}
		} else {
			JXMapWindow(display, event->window);
		}
		JXSync(display, False);
		JXUngrabServer(display);
	} else {
		if(!(np->state.status & STAT_MAPPED)) {
			np->state.status |= STAT_MAPPED;
			np->state.status &= ~STAT_MINIMIZED;
			np->state.status &= ~STAT_SDESKTOP;
			JXMapWindow(display, np->window);
			JXMapWindow(display, np->parent);
			RaiseClient(np);
			if(focusModel == FOCUS_CLICK) {
				FocusClient(np);
			}
			UpdateTaskBar();
			UpdatePager();
		}
	}
	RestackClients();
}

/****************************************************************************
 ****************************************************************************/
void HandleUnmapNotify(const XUnmapEvent *event) {

	ClientNode *np;
	XEvent e;

	Assert(event);

	np = FindClientByWindow(event->window);
	if(np && np->window == event->window) {

		if(JXCheckTypedWindowEvent(display, np->window, DestroyNotify, &e)) {
			HandleDestroyNotify(&e.xdestroywindow);
			return;
		}

		if(np->controller) {
			(np->controller)(1);
		}

		if(np->state.status & STAT_MAPPED) {

			np->state.status &= ~STAT_MAPPED;
			JXUnmapWindow(display, np->parent);

			WriteState(np);
			UpdateTaskBar();
			UpdatePager();

		}

	} else if(!np) {

		HandleDockDestroy(event->window);

	}

}

/****************************************************************************
 ****************************************************************************/
int HandleDestroyNotify(const XDestroyWindowEvent *event) {

	ClientNode *np;

	np = FindClientByWindow(event->window);
	if(np && np->window == event->window) {

		if(np->controller) {
			(np->controller)(1);
		}

		RemoveClient(np);

		return 1;

	} else if(!np) {

		return HandleDockDestroy(event->window);

	}

	return 0;

}

/****************************************************************************
 ****************************************************************************/
void DispatchBorderButtonEvent(const XButtonEvent *event, ClientNode *np) {

	static Time lastClickTime = 0;
	static int lastX = 0, lastY = 0;
	static int doubleClickActive = 0;
	BorderActionType action;
	int bsize;

	action = GetBorderActionType(np, event->x, event->y);

	switch(action & 0x0F) {
	case BA_RESIZE:
		if(event->type == ButtonPress) {
			ResizeClient(np, action, event->x, event->y);
		}
		break;
	case BA_MOVE:
		if(event->type == ButtonPress) {
			if(doubleClickActive
				&& abs(event->time - lastClickTime) > 0
				&& abs(event->time - lastClickTime) <= doubleClickSpeed
				&& abs(event->x - lastX) <= doubleClickDelta
				&& abs(event->y - lastY) <= doubleClickDelta) {
				MaximizeClient(np);
				doubleClickActive = 0;
			} else {
				if(MoveClient(np, event->x, event->y)) {
					doubleClickActive = 0;
				} else {
					doubleClickActive = 1;
					lastClickTime = event->time;
					lastX = event->x;
					lastY = event->y;
				}
			}
		}
		break;
	case BA_MENU:
		if(event->type == ButtonPress) {
			if(np->state.border & BORDER_OUTLINE) {
				bsize = borderWidth;
			} else {
				bsize = 0;
			}
			ShowWindowMenu(np, np->x + event->x - bsize,
				np->y + event->y - titleHeight - bsize);
		}
		break;
	case BA_CLOSE:
		if(event->type == ButtonRelease) {
			DeleteClient(np);
		}
		break;
	case BA_MAXIMIZE:
		if(event->type == ButtonRelease) {
			MaximizeClient(np);
		}
		break;
	case BA_MINIMIZE:
		if(event->type == ButtonRelease) {
			MinimizeClient(np);
		}
		break;
	default:
		break;
	}

}

