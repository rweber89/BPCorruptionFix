#pragma once

#include "CoreMinimal.h"

class FBPCorruptionFixModule : public IModuleInterface
{
public:
	void StartupModule() override;
	void ShutdownModule() override;

private:
	void RegisterMenus();

	void RegisterCopyAction( FToolMenuSection& Section );
	void RegisterPasteAction( FToolMenuSection& Section );
};
