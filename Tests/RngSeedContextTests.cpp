/*=============================================================================
	RngSeedContextTests.cpp: Exact Context seed bootstrap contract.
=============================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "Core.h"
#include "Support/HP2TestProcess.h"

extern void appFlushRandTrace();

#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#include <cstdlib>
#else
#include <unistd.h>
#endif

namespace
{
	void SetEnvironment(const char* Name, const char* Value)
	{
#if defined(_WIN32)
		(void)_putenv_s(Name, Value);
#else
		(void)setenv(Name, Value, 1);
#endif
	}

	void UnsetEnvironment(const char* Name)
	{
#if defined(_WIN32)
		(void)_putenv_s(Name, "");
#else
		(void)unsetenv(Name);
#endif
	}
	void Require( int Condition, const char* Message )
	{
		if( !Condition )
		{
			fprintf( stderr, "RngSeedContextTests: %s\n", Message );
			exit(1);
		}
	}

	void RequireNextRandAfterContext( unsigned Seed )
	{
		srand( Seed );
		const int Expected = rand();
		srand( Seed );
		__Context::StaticInit();
		Require( rand()==Expected, "Context consumed or changed the seeded stream" );
	}

	void RequireInvalidSeedFailsBeforeAppInit()
	{
		std::string Error;
		const int Status = HP2TestProcess::RunSelf(
			{"invalid-seed-child"}, {"HP2_RNG_SEED=invalid"}, Error);
		Require(Status == 1, "invalid seed did not exit one");
		char Expected[160];
		snprintf(
			Expected,
			sizeof(Expected),
			"Invalid HP2_RNG_SEED: expected an unsigned decimal value from 0 to %u\n",
			(unsigned)RAND_MAX
		);
		Require(Error == Expected, "invalid seed stderr changed");
	}

	void RequireTraceCanArmAfterContext()
	{
		const char* ArtifactDirectory = getenv( "HP2_ARTIFACT_DIR" );
		Require( ArtifactDirectory && *ArtifactDirectory, "HP2_ARTIFACT_DIR is required" );
		char TracePath[512];
		const int PathLength = snprintf(
			TracePath,
			sizeof(TracePath),
			"%s/context-post-seam-rng-trace.json",
			ArtifactDirectory
		);
		Require( PathLength>0 && (size_t)PathLength<sizeof(TracePath), "trace path buffer too small" );
		UnsetEnvironment("HP2_RNG_TRACE");
		SetEnvironment("HP2_RNG_SEED", "42");
		__Context::StaticInit();
		SetEnvironment("HP2_RNG_TRACE", TracePath);
		appRand();
		appFlushRandTrace();
		std::ifstream Trace(TracePath, std::ios::binary);
		Require(Trace.good(), "Context initialized tracing before trace environment existed");
	}
}

int main( int ArgC, char* ArgV[] )
{
	Require( ArgC==2, "expected default, diagnostic, invalid, or late_trace mode" );
	if( !strcmp(ArgV[1],"invalid-seed-child") )
	{
		__Context::StaticInit();
		return 0;
	}
	if( !strcmp(ArgV[1],"default") )
	{
		UnsetEnvironment("HP2_RNG_SEED");
		RequireNextRandAfterContext( 42 );
	}
	else if( !strcmp(ArgV[1],"diagnostic") )
	{
		SetEnvironment("HP2_RNG_SEED", "0");
		RequireNextRandAfterContext( 1 );
	}
	else if( !strcmp(ArgV[1],"invalid") )
		RequireInvalidSeedFailsBeforeAppInit();
	else if( !strcmp(ArgV[1],"late_trace") )
		RequireTraceCanArmAfterContext();
	else
		Require( 0, "unknown mode" );

	puts("RngSeedContextTests: PASS");
	return 0;
}
