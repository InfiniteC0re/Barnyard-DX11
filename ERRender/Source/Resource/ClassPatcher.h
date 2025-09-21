#pragma once

#define TDEFINE_CLASS_PATCHED( CLASS, ADDRESS ) \
	TDEFINE_CLASS_FULL( CLASS ) \
	Toshi::TClass CLASS::TClassObjectName = *(Toshi::TClass*)( ADDRESS );
