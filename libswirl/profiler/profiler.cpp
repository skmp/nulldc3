/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "profiler.h"

profiler_cfg prof;

void prof_init()
{
	memset(&prof,0,sizeof(prof));
	prof.enable=false;
}

struct regacc
{
	Sh4RegType reg;

	u32 sum;
	u32 r;
	u32 w;
	u32 rw;

	void print()
	{
		printf("%02d: %d || %d %d %d\n",reg,sum,r,w,rw);
	}

	bool operator< (const regacc& rhs) const
	{
		return sum>rhs.sum;
	}
};

#include "hw/sh4/sh4_opcode_list.h"

int stuffcmp(const void* p1,const void* p2)
{
	sh4_opcodelistentry* a=(sh4_opcodelistentry*)p1;
	sh4_opcodelistentry* b=(sh4_opcodelistentry*)p2;

	return (int)(b->fallbacks-a->fallbacks);
}

u32 SQW,DMAW;

u32 TA_VTXC,TA_SPRC,TA_EOSC,TA_PPC,TA_SPC,TA_EOLC,TA_V64HC;

u32 PVR_VTXC;

extern u32 samples_gen;

void print_blocks();

#if FEAT_SHREC != DYNAREC_NONE
//called every emulated second
void prof_periodical()
{
#if defined(HAS_PROFILE)
#if 0
	printf("SQW %d,DMAW %d\n",SQW,DMAW);
	DMAW=SQW=0;
#endif

#if FEAT_SHREC != DYNAREC_NONE
	print_blocks();
#endif

	return;

	printf("TA_VTXC %d,TA_SPRC %d,TA_EOSC %d,TA_PPC %d,TA_SPC %d,TA_EOLC %d,TA_V64HC %d\n", TA_VTXC,TA_SPRC,TA_EOSC,TA_PPC,TA_SPC,TA_EOLC,TA_V64HC);
	TA_VTXC=TA_SPRC=TA_EOSC=TA_PPC=TA_SPC=TA_EOLC=TA_V64HC=0;



	//for (u32 i=0;i<all_blocks
	return;

	#if HOST_OS!=OS_WINDOWS
		return;
	#endif

	printf("Samples Gen: %.2f %.2f\n",samples_gen/1000.0,samples_gen/44100.0);
	samples_gen=0;
	return;

	for(int i=0;i<shop_max;i++)
	{
		double v=prof.counters.shil.executed[i]/1000.0/1000.0;
		prof.counters.shil.executed[i]=0;
		if (v>0.05)
			printf("%s: %.2fM\n",shil_opcode_name(i),v);
	}

	
	if (prof.counters.shil.readm_reg!=0)
	{
		printf("***PROFILE REPORT***\n");
		prof.counters.print();
	}

	printf("opcode fallbacks:\n");
	vector<sh4_opcodelistentry> stuff;
	for (u32 i=0;opcodes[i].oph;i++)
	{
		if (opcodes[i].fallbacks)
			stuff.push_back(opcodes[i]);
	}

	if (stuff.size())
	{
		qsort(&stuff[0],stuff.size(),sizeof(stuff[0]),stuffcmp);
		for (u32 i=0;i<10 && i<stuff.size();i++)
		{
			printf("%05I64u :%04X %s\n",stuff[i].fallbacks,stuff[i].rez,stuff[i].diss);
		}
	}

	for (u32 i=0;opcodes[i].oph;i++)
	{
		opcodes[i].fallbacks=0;
	}

	printf("********************\n");
	memset(&prof.counters,0,sizeof(prof.counters));
#endif
}
#else
void prof_periodical() { }
#endif
/*

	debprof: a remote debugger/profiler


	Text based i/o

	var = x
	varN = { .. }


	Commands:
	name(par1=x,par2=y)

	
*/
