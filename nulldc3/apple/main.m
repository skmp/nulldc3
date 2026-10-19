/*
    This file is part of reicast-osx
*/
#include "license/bsd"

#import <AppKit/AppKit.h>
#import "AppDelegate.h"
#import "MainMenu.h"

#include <mach/mach_init.h>
#include <mach/mach_port.h>

int main(int argc, char *argv[]) {
    task_set_exception_ports(
                            mach_task_self(),
                            EXC_MASK_BAD_ACCESS,
                            MACH_PORT_NULL,//m_exception_port,
                            EXCEPTION_DEFAULT,
                            0);
    @autoreleasepool {
        // Create Application and AppDelegate
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app setDelegate:(id)[[AppDelegate alloc] initWithArgc:argc andArgv:argv]];
        
        // Create the Main Menu
        [MainMenu create];
             
        // Start the Application
        [app activateIgnoringOtherApps:YES];
        [app run];
        
        // Return success when we exit
        return EXIT_SUCCESS;
    }
}
