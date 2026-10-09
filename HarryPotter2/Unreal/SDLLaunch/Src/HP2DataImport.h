/*=============================================================================
	HP2DataImport.h: In-process retail game-data import service.
=============================================================================*/
#ifndef HP2DATAIMPORT_H
#define HP2DATAIMPORT_H

#include "HP2Import.h"

#include <string>

namespace HP2DataImport
{

bool GetConventionalRetailDestination(std::string& Destination);
bool RunRetailImport(
	const std::string& SourcePath,
	const std::string& Destination,
	HP2Import::Progress* Progress,
	std::string& Output,
	std::string& Error);

}

#endif
