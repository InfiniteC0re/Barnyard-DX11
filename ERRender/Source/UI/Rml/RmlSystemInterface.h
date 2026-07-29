#pragma once
#include <RmlUi/Core/SystemInterface.h>

namespace remaster
{

class RmlSystemInterface : public Rml::SystemInterface
{
public:
	RmlSystemInterface();

	virtual double GetElapsedTime() OVERRIDE;
	virtual bool   LogMessage( Rml::Log::Type a_eType, const Rml::String& a_rMessage ) OVERRIDE;

	// Localisation hook. Replaces [[N]] markers with the game's localised string N,
	// then expands %c_Map[Command] button-prompt tokens via the input system
	virtual int TranslateString( Rml::String& a_rTranslated, const Rml::String& a_rInput ) OVERRIDE;

private:
	TINT64 m_iStartTicks;
	TINT64 m_iTicksPerSecond;
};

} // namespace remaster
