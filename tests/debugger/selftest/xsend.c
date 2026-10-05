/*
	xsend: what a person does to the window of the emulator, for window_checklist.sh.

	    xsend list                       prints the ids of the windows that have a name. The emulator sets
	                                     nothing on its window that tells whose it is: the window of a run is
	                                     the one that is listed after its start and was not before
	    xsend key <window> <keycode>     a key pressed and released (keycodes of the X server: 23 Tab, 9 Escape)
	    xsend click <window> <x> <y>     the left button pressed and released at a point of the window
	    xsend close <window>             the close button of the window manager
	    xsend title <window>             prints the name of the window
	    xsend size <window>              prints its width and height

	The events are sent to the window (XSendEvent): nothing is typed or clicked on the desktop, and the
	pointer does not move.

	    cc -o xsend xsend.c -lX11
*/
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display* display;

static int has_name(Window w)
{
	XTextProperty name;

	if (!XGetWMName(display, w, &name) || name.value == NULL)
		return 0;

	XFree(name.value);
	return 1;
}

/* Every window that has a name, the ones of the window manager included */
static void list(Window w, int depth)
{
	Window root, parent, *children = NULL;
	unsigned int count = 0;

	if (depth > 0 && has_name(w))
		printf("0x%lx\n", (unsigned long)w);

	if (!XQueryTree(display, w, &root, &parent, &children, &count))
		return;

	for (unsigned int i = 0; i < count; i++)
		list(children[i], depth + 1);

	if (children)
		XFree(children);
}

int main(int argc, char** argv)
{
	if (argc < 2 || (argc < 3 && strcmp(argv[1], "list") != 0))
	{
		fprintf(stderr, "usage: xsend list | key <window> <keycode> | click <window> <x> <y> | close <window> | title <window> | size <window>\n");
		return 2;
	}

	display = XOpenDisplay(NULL);

	if (!display)
	{
		fprintf(stderr, "xsend: no display\n");
		return 1;
	}

	const char* what = argv[1];

	if (strcmp(what, "list") == 0)
	{
		list(DefaultRootWindow(display), 0);
		return 0;
	}

	Window w = (Window)strtoul(argv[2], NULL, 0);
	XWindowAttributes attr;

	if (!XGetWindowAttributes(display, w, &attr))
		return 1;

	if (strcmp(what, "key") == 0 && argc == 4)
	{
		XEvent e;

		memset(&e, 0, sizeof(e));
		e.xkey.display = display;
		e.xkey.window = w;
		e.xkey.root = attr.root;
		e.xkey.subwindow = None;
		e.xkey.time = CurrentTime;
		e.xkey.same_screen = True;
		e.xkey.keycode = (unsigned int)atoi(argv[3]);

		e.type = KeyPress;
		XSendEvent(display, w, False, KeyPressMask, &e);
		XFlush(display);
		usleep(50000);

		e.type = KeyRelease;
		XSendEvent(display, w, False, KeyReleaseMask, &e);
	}
	else if (strcmp(what, "click") == 0 && argc == 5)
	{
		XEvent e;
		int x = atoi(argv[3]), y = atoi(argv[4]);

		memset(&e, 0, sizeof(e));
		e.type = MotionNotify;
		e.xmotion.display = display;
		e.xmotion.window = w;
		e.xmotion.root = attr.root;
		e.xmotion.x = x;
		e.xmotion.y = y;
		e.xmotion.same_screen = True;
		XSendEvent(display, w, False, PointerMotionMask, &e);
		XFlush(display);
		usleep(150000);

		memset(&e, 0, sizeof(e));
		e.xbutton.display = display;
		e.xbutton.window = w;
		e.xbutton.root = attr.root;
		e.xbutton.x = x;
		e.xbutton.y = y;
		e.xbutton.button = Button1;
		e.xbutton.same_screen = True;

		e.type = ButtonPress;
		XSendEvent(display, w, False, ButtonPressMask, &e);
		XFlush(display);
		usleep(150000);

		e.type = ButtonRelease;
		XSendEvent(display, w, False, ButtonReleaseMask, &e);
	}
	else if (strcmp(what, "close") == 0)
	{
		XEvent e;

		memset(&e, 0, sizeof(e));
		e.type = ClientMessage;
		e.xclient.window = w;
		e.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", False);
		e.xclient.format = 32;
		e.xclient.data.l[0] = (long)XInternAtom(display, "WM_DELETE_WINDOW", False);
		e.xclient.data.l[1] = CurrentTime;
		XSendEvent(display, w, False, NoEventMask, &e);
	}
	else if (strcmp(what, "title") == 0)
	{
		Atom type;
		int format;
		unsigned long count, left;
		unsigned char* data = NULL;

		if (XGetWindowProperty(display, w, XInternAtom(display, "WM_NAME", False), 0, 1024, False, AnyPropertyType,
				&type, &format, &count, &left, &data) == Success && data != NULL)
		{
			printf("%s\n", (const char*)data);
			XFree(data);
		}
	}
	else if (strcmp(what, "size") == 0)
	{
		printf("%d %d\n", attr.width, attr.height);
	}
	else
	{
		fprintf(stderr, "xsend: unknown command\n");
		return 2;
	}

	XFlush(display);
	XCloseDisplay(display);
	return 0;
}
