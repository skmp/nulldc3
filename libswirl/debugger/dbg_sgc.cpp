/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_sgc.h"

/*
	The stubs of header revision 7: the declarations of dbg_sgc.h and of the SGC block of debugger.h, with the
	bodies of a build that has no capture. Nothing is ever armed (Arm() answers kErrNotSupported), no frame is
	taken and the server has no buffer. The core's notice is kept as it is installed, and never given.

	The state is plain data with static storage: nothing here has a destructor.
*/

u32 dbg_sgc_active;

namespace {

void (*completion_notice)(int answer);

}

u32 dbg_sgc_Frame(bool)
{
	return 0;
}

namespace dbg { namespace sgc {

int Arm(u32, bool)
{
	return kErrNotSupported;
}

void Cancel(int)
{
}

State GetState(u32* frames)
{
	*frames = 0;
	return State::None;
}

u32 Copy(std::string*)
{
	return 0;
}

void SetCompletionNotice(void (*notice)(int answer))
{
	completion_notice = notice;
}

} }
