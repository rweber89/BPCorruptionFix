#include "BPCorruptionFix.h"

#include "ObjectEditorUtils.h"
#include "ToolMenus.h"
#include "SSubobjectEditor.h"
#include "SSubobjectInstanceEditor.h"
#include "SubobjectEditorMenuContext.h"
#include "BlueprintEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/BufferWriter.h"

#define LOCTEXT_NAMESPACE "BPCorruptionFix"

const UObject* g_ToCopy = nullptr;
const FObjectProperty* g_PropToCopy = nullptr;

namespace
{
	bool GetSubobjectEditorFromContext( const FToolMenuContext& InContext, TSharedPtr<SSubobjectEditor>& SubobjectEditor )
	{
		USubobjectEditorMenuContext* ContextObject = InContext.FindContext<USubobjectEditorMenuContext>();
		if( !ContextObject )
		{
			return false;
		}

		SubobjectEditor = ContextObject->SubobjectEditor.Pin();
		if ( !SubobjectEditor.IsValid() || !StaticCastSharedPtr<SSubobjectInstanceEditor>(SubobjectEditor))
		{
			return false;
		}

		return true;
	}

	FObjectProperty* GetPropertyForNode(FSubobjectEditorTreeNodePtrType Node)
	{
		UObject* ComponentOuter = Node->GetObject()->GetOuter();
		UClass* OwnerClass = ComponentOuter->GetClass();

		for (TFieldIterator<FObjectProperty> It(OwnerClass); It; ++It)
		{
			FObjectProperty* ObjectProp = *It;

			// Must be visible - note CPF_Edit is set for all properties that should be visible, not just those that are editable
			if ((ObjectProp->PropertyFlags & (CPF_Edit)) == 0)
			{
				//UE_LOG(LogTemp, Error, TEXT("NotEditable: %s"), *ObjectProp->GetName());
				continue;
			}

			if (Node->GetVariableName().ToString() == ObjectProp->GetName())
			{
				return ObjectProp;
			}

			// NOTE [RW] keep here for posterity. If ever built out to automatically detect, this would be how, whilst iterating over the properties
			// if( !ObjectProp->GetObjectPropertyValue(ObjectProp->ContainerPtrToValuePtr<void>(ComponentOuter)) )
			// { Code here to signal detected broken property }
		}

		return nullptr;
	}

	bool GetEditorsFromContext( const FToolMenuContext& InContext, TSharedPtr<SSubobjectEditor>& SubobjectEditor, TSharedPtr<FBlueprintEditor>& Editor )
	{
		if( !GetSubobjectEditorFromContext( InContext, SubobjectEditor ) )
		{
			return false;
		}
		
		const auto& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
		for( const auto& OpenEditor : BlueprintEditorModule.GetBlueprintEditors() )
		{
			const TSharedRef<FBlueprintEditor>& OpenBlueprintEditor = StaticCastSharedRef<FBlueprintEditor>(OpenEditor);
			if( OpenBlueprintEditor->GetSubobjectEditor() == SubobjectEditor )
			{
				Editor = OpenBlueprintEditor;
				return true;
			}
		}

		return false;
	}

	auto GetIsVisibleLambda()
	{
		return FToolMenuIsActionButtonVisible::CreateLambda([](const FToolMenuContext& InContext)
		{
			TSharedPtr<SSubobjectEditor> SubobjectEditor;
			if( !GetSubobjectEditorFromContext( InContext, SubobjectEditor ) )
			{
				return false;
			}
			
			return SubobjectEditor->GetSelectedNodes().Num() == 1;
		});
	}
}

void FBPCorruptionFix::StartupModule()
{
	RegisterMenus();
}

void FBPCorruptionFix::ShutdownModule()
{
}

void FBPCorruptionFix::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("Kismet.SubobjectEditorContextMenu");
	FToolMenuSection& Section = Menu->AddSection("BPCorruptionFixctions", LOCTEXT("BPCorruptionFixActionsHeader", "BPCorruptionFix"));
	
	RegisterCopyAction( Section );
	RegisterPasteAction( Section );
}

void FBPCorruptionFix::RegisterCopyAction(FToolMenuSection& Section)
{
	FToolUIAction Action;
	Action.CanExecuteAction = FToolMenuCanExecuteAction::CreateLambda([&](const FToolMenuContext& InContext)
	{
		TSharedPtr<SSubobjectEditor> SubobjectEditor;
		if (!GetSubobjectEditorFromContext(InContext, SubobjectEditor))
		{
			return false;
		}

		const auto Node = SubobjectEditor->GetSelectedNodes()[0];
		const auto* TargetProperty = GetPropertyForNode(Node);
		if (!TargetProperty)
		{
			return false;
		}

		// Make sure to not copy a broken property
		if (!TargetProperty->GetObjectPropertyValue(
			TargetProperty->ContainerPtrToValuePtr<void>(Node->GetObject()->GetOuter())))
		{
			return false;
		}

		return true;
	});
	
	Action.ExecuteAction = FToolMenuExecuteAction::CreateLambda([&](const FToolMenuContext& InContext)
	{
		TSharedPtr<SSubobjectEditor> SubobjectEditor;
		TSharedPtr<FBlueprintEditor> Editor;
		if( !GetEditorsFromContext( InContext, SubobjectEditor, Editor ) )
		{
			return;
		}

		const auto Node = SubobjectEditor->GetSelectedNodes()[0];
		const auto* TargetProperty = GetPropertyForNode( Node );
		if( !TargetProperty )
		{
			return;
		}

		g_PropToCopy = TargetProperty;
		g_ToCopy = Node->GetObject()->GetOuter();
	});

	Action.IsActionVisibleDelegate = GetIsVisibleLambda();
	
	FToolMenuEntry& Entry = Section.AddMenuEntry(
		"CopyIntactProperties",
		LOCTEXT( "CopyIntactLabel", "Copy Intact Subobject"),
		LOCTEXT( "CopyAllTooltip", "Copies the intact suboject so that it may be pasted onto a broken one"),
		FSlateIcon(),
		Action,
		EUserInterfaceActionType::Button);
}

void FBPCorruptionFix::RegisterPasteAction(FToolMenuSection& Section)
{
	FToolUIAction Action;
	Action.CanExecuteAction = FToolMenuCanExecuteAction::CreateLambda([&](const FToolMenuContext& InContext)
	{
		if( !g_ToCopy )
		{
			return false;
		}

		TSharedPtr<SSubobjectEditor> SubobjectEditor;
		if( !GetSubobjectEditorFromContext( InContext, SubobjectEditor ) )
		{
			return false;
		}
		
		const auto Node = SubobjectEditor->GetSelectedNodes()[0];
		const auto* TargetProperty = GetPropertyForNode( Node );
		if( !TargetProperty )
		{
			return false;
		}

		// Make sure that we never try to copy across different classes
		if( g_PropToCopy->PropertyClass != TargetProperty->PropertyClass )
		{
			return false;
		}

		// The names need to match to make sure we copy the right data
		if( g_PropToCopy->GetName() != TargetProperty->GetName() )
		{
			return false;
		}

		// Only allow fixing broken properties
		if( TargetProperty->GetObjectPropertyValue(TargetProperty->ContainerPtrToValuePtr<void>(Node->GetObject()->GetOuter())) )
		{
			return false;
		}

		return true;
	});
	
	Action.ExecuteAction = FToolMenuExecuteAction::CreateLambda([&](const FToolMenuContext& InContext)
	{
		TSharedPtr<SSubobjectEditor> SubobjectEditor;
		TSharedPtr<FBlueprintEditor> Editor;
		if( !GetEditorsFromContext( InContext, SubobjectEditor, Editor ) )
		{
			return;
		}

		const auto Node = SubobjectEditor->GetSelectedNodes()[0];

		// Copy from the intact to the broken memory
		const uint8* SourcePtr = g_PropToCopy->ContainerPtrToValuePtr<uint8>(g_ToCopy);
		uint8* DestPtr = g_PropToCopy->ContainerPtrToValuePtr<uint8>(Node->GetObject()->GetOuter());
		g_PropToCopy->CopyCompleteValue(DestPtr, SourcePtr);

		// Refresh UI
		SubobjectEditor->RefreshSelectionDetails();
	});

	Action.IsActionVisibleDelegate = GetIsVisibleLambda();
	
	FToolMenuEntry& Entry = Section.AddMenuEntry(
		"PasteIntactProperties",
		LOCTEXT( "PasteIntactLabel", "Paste Intact Subobject"),
		LOCTEXT( "PasteIntactTooltip", "Pastes the intact Subobject inplace to fix a broken one (Inheriting BPs still need to be fixed manaully)"),
		FSlateIcon(),
		Action,
		EUserInterfaceActionType::Button);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE( FBPCorruptionFix, BPCorruptionFix)
