#pragma once

#include <obs-module.h>

#include <QString>

inline QString Tr(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

