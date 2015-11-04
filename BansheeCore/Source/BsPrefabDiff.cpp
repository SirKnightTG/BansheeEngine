#include "BsPrefabDiff.h"
#include "BsPrefabDiffRTTI.h"
#include "BsSceneObject.h"
#include "BsMemorySerializer.h"
#include "BsBinarySerializer.h"
#include "BsBinaryDiff.h"

namespace BansheeEngine
{
	RTTITypeBase* PrefabComponentDiff::getRTTIStatic()
	{
		return PrefabComponentDiffRTTI::instance();
	}

	RTTITypeBase* PrefabComponentDiff::getRTTI() const
	{
		return PrefabComponentDiff::getRTTIStatic();
	}

	RTTITypeBase* PrefabObjectDiff::getRTTIStatic()
	{
		return PrefabObjectDiffRTTI::instance();
	}

	RTTITypeBase* PrefabObjectDiff::getRTTI() const
	{
		return PrefabObjectDiff::getRTTIStatic();
	}

	SPtr<PrefabDiff> PrefabDiff::create(const HSceneObject& prefab, const HSceneObject& instance)
	{
		if (prefab->mPrefabLinkUUID != instance->mPrefabLinkUUID)
			return nullptr;

		// Note: If this method is called multiple times in a row then renaming all objects every time is redundant, it
		// would be more efficient to do it once outside of this method. I'm keeping it this way for simplicity for now.

		Vector<RenamedGameObject> renamedObjects;
		renameInstanceIds(prefab, instance, renamedObjects);

		SPtr<PrefabDiff> output = bs_shared_ptr_new<PrefabDiff>();
		output->mRoot = generateDiff(prefab, instance);

		restoreInstanceIds(renamedObjects);

		return output;
	}

	void PrefabDiff::apply(const HSceneObject& object)
	{
		if (mRoot == nullptr)
			return;

		GameObjectManager::instance().startDeserialization();
		applyDiff(mRoot, object);
		GameObjectManager::instance().endDeserialization();
	}

	void PrefabDiff::applyDiff(const SPtr<PrefabObjectDiff>& diff, const HSceneObject& object)
	{
		object->setName(diff->name);

		// Note: It is important to remove objects and components first, before adding them.
		//		 Some systems rely on the fact that applyDiff added components/objects are 
		//       always at the end.
		const Vector<HComponent>& components = object->getComponents();
		for (auto& removedId : diff->removedComponents)
		{
			for (auto component : components)
			{
				if (removedId == component->getLinkId())
				{
					component->destroy();
					break;
				}
			}
		}

		for (auto& removedId : diff->removedChildren)
		{
			UINT32 childCount = object->getNumChildren();
			for (UINT32 i = 0; i < childCount; i++)
			{
				HSceneObject child = object->getChild(i);
				if (removedId == child->getLinkId())
				{
					child->destroy();
					break;
				}
			}
		}

		for (auto& addedComponentData : diff->addedComponents)
		{
			BinarySerializer bs;
			SPtr<Component> component = std::static_pointer_cast<Component>(bs._decodeIntermediate(addedComponentData));
			object->addComponentInternal(component);
		}

		for (auto& addedChildData : diff->addedChildren)
		{
			BinarySerializer bs;
			SPtr<SceneObject> sceneObject = std::static_pointer_cast<SceneObject>(bs._decodeIntermediate(addedChildData));
			sceneObject->setParent(object);
			sceneObject->instantiate();
		}

		for (auto& componentDiff : diff->componentDiffs)
		{
			for (auto& component : components)
			{
				if (componentDiff->id == component->getLinkId())
				{
					IDiff& diffHandler = component->getRTTI()->getDiffHandler();
					diffHandler.applyDiff(component.getInternalPtr(), componentDiff->data);
					break;
				}
			}
		}

		for (auto& childDiff : diff->childDiffs)
		{
			UINT32 childCount = object->getNumChildren();
			for (UINT32 i = 0; i < childCount; i++)
			{
				HSceneObject child = object->getChild(i);
				if (childDiff->id == child->getLinkId())
				{
					applyDiff(childDiff, child);
					break;
				}
			}
		}
	}

	SPtr<PrefabObjectDiff> PrefabDiff::generateDiff(const HSceneObject& prefab, const HSceneObject& instance)
	{
		SPtr<PrefabObjectDiff> output;

		if (prefab->getName() != instance->getName())
		{
			if (output == nullptr)
				output = bs_shared_ptr_new<PrefabObjectDiff>();
		}

		UINT32 prefabChildCount = prefab->getNumChildren();
		UINT32 instanceChildCount = instance->getNumChildren();

		// Find modified and removed children
		for (UINT32 i = 0; i < prefabChildCount; i++)
		{
			HSceneObject prefabChild = prefab->getChild(i);

			SPtr<PrefabObjectDiff> childDiff;
			bool foundMatching = false;
			for (UINT32 j = 0; j < instanceChildCount; j++)
			{
				HSceneObject instanceChild = instance->getChild(j);

				if (prefabChild->getLinkId() == instanceChild->getLinkId())
				{
					if (instanceChild->mPrefabLinkUUID.empty())
						childDiff = generateDiff(prefabChild, instanceChild);

					foundMatching = true;
					break;
				}
			}

			if (foundMatching)
			{
				if (childDiff != nullptr)
				{
					if (output == nullptr)
						output = bs_shared_ptr_new<PrefabObjectDiff>();

					output->childDiffs.push_back(childDiff);
				}
			}
			else
			{
				if (output == nullptr)
					output = bs_shared_ptr_new<PrefabObjectDiff>();

				output->removedChildren.push_back(prefabChild->getLinkId());
			}	
		}

		// Find added children
		for (UINT32 i = 0; i < instanceChildCount; i++)
		{
			HSceneObject instanceChild = instance->getChild(i);

			if (instanceChild->hasFlag(SOF_DontSave))
				continue;

			bool foundMatching = false;
			if (instanceChild->getLinkId() != -1)
			{
				for (UINT32 j = 0; j < prefabChildCount; j++)
				{
					HSceneObject prefabChild = prefab->getChild(j);

					if (prefabChild->getLinkId() == instanceChild->getLinkId())
					{
						foundMatching = true;
						break;
					}
				}
			}

			if (!foundMatching)
			{
				BinarySerializer bs;
				SPtr<SerializedObject> obj = bs._encodeIntermediate(instanceChild.get());

				if (output == nullptr)
					output = bs_shared_ptr_new<PrefabObjectDiff>();

				output->addedChildren.push_back(obj);
			}
		}

		const Vector<HComponent>& prefabComponents = prefab->getComponents();
		const Vector<HComponent>& instanceComponents = instance->getComponents();

		UINT32 prefabComponentCount = (UINT32)prefabComponents.size();
		UINT32 instanceComponentCount = (UINT32)instanceComponents.size();

		// Find modified and removed components
		for (UINT32 i = 0; i < prefabComponentCount; i++)
		{
			HComponent prefabComponent = prefabComponents[i];

			SPtr<PrefabComponentDiff> childDiff;
			bool foundMatching = false;
			for (UINT32 j = 0; j < instanceComponentCount; j++)
			{
				HComponent instanceComponent = instanceComponents[j];

				if (prefabComponent->getLinkId() == instanceComponent->getLinkId())
				{
					BinarySerializer bs;
					SPtr<SerializedObject> encodedPrefab = bs._encodeIntermediate(prefabComponent.get());
					SPtr<SerializedObject> encodedInstance = bs._encodeIntermediate(instanceComponent.get());

					IDiff& diffHandler = prefabComponent->getRTTI()->getDiffHandler();
					SPtr<SerializedObject> diff = diffHandler.generateDiff(encodedPrefab, encodedInstance);

					if (diff != nullptr)
					{
						childDiff = bs_shared_ptr_new<PrefabComponentDiff>();
						childDiff->id = prefabComponent->getLinkId();
						childDiff->data = diff;
					}

					foundMatching = true;
					break;
				}
			}

			if (foundMatching)
			{
				if (childDiff != nullptr)
				{
					if (output == nullptr)
						output = bs_shared_ptr_new<PrefabObjectDiff>();

					output->componentDiffs.push_back(childDiff);
				}
			}
			else
			{
				if (output == nullptr)
					output = bs_shared_ptr_new<PrefabObjectDiff>();

				output->removedComponents.push_back(prefabComponent->getLinkId());
			}
		}

		// Find added components
		for (UINT32 i = 0; i < instanceComponentCount; i++)
		{
			HComponent instanceComponent = instanceComponents[i];

			bool foundMatching = false;
			if (instanceComponent->getLinkId() != -1)
			{
				for (UINT32 j = 0; j < prefabComponentCount; j++)
				{
					HComponent prefabComponent = prefabComponents[j];

					if (prefabComponent->getLinkId() == instanceComponent->getLinkId())
					{
						foundMatching = true;
						break;
					}
				}
			}

			if (!foundMatching)
			{
				BinarySerializer bs;
				SPtr<SerializedObject> obj = bs._encodeIntermediate(instanceComponent.get());

				if (output == nullptr)
					output = bs_shared_ptr_new<PrefabObjectDiff>();

				output->addedComponents.push_back(obj);
			}
		}

		if (output != nullptr)
		{
			output->name = instance->getName();
			output->id = instance->getLinkId();
		}

		return output;
	}

	void PrefabDiff::renameInstanceIds(const HSceneObject& prefab, const HSceneObject& instance, Vector<RenamedGameObject>& output)
	{
		UnorderedMap<String, UnorderedMap<UINT32, UINT64>> linkToInstanceId;

		struct StackEntry
		{
			HSceneObject so;
			String uuid;
		};

		Stack<StackEntry> todo;
		todo.push({ prefab, "root" });

		while (!todo.empty())
		{
			StackEntry current = todo.top();
			todo.pop();

			String childParentUUID;
			if (current.so->mPrefabLinkUUID.empty())
				childParentUUID = current.uuid;
			else
				childParentUUID = current.so->mPrefabLinkUUID;

			UnorderedMap<UINT32, UINT64>& idMap = linkToInstanceId[childParentUUID];

			const Vector<HComponent>& components = current.so->getComponents();
			for (auto& component : components)
				idMap[component->getLinkId()] = component->getInstanceId();

			UINT32 numChildren = current.so->getNumChildren();
			for (UINT32 i = 0; i < numChildren; i++)
			{
				HSceneObject child = current.so->getChild(i);

				idMap[child->getLinkId()] = child->getInstanceId();
				todo.push({ child, childParentUUID });
			}
		}

		// Root has link ID from its parent so we handle it separately
		{
			output.push_back(RenamedGameObject());
			RenamedGameObject& renamedGO = output.back();
			renamedGO.instanceData = instance->mInstanceData;
			renamedGO.originalId = instance->getInstanceId();

			instance->mInstanceData->mInstanceId = prefab->getInstanceId();
		}

		todo.push({ instance, "root" });
		while (!todo.empty())
		{
			StackEntry current = todo.top();
			todo.pop();

			String childParentUUID;
			if (current.so->mPrefabLinkUUID.empty())
				childParentUUID = current.uuid;
			else
				childParentUUID = current.so->mPrefabLinkUUID;

			auto iterFind = linkToInstanceId.find(childParentUUID);
			if (iterFind != linkToInstanceId.end())
			{
				UnorderedMap<UINT32, UINT64>& idMap = iterFind->second;

				const Vector<HComponent>& components = current.so->getComponents();
				for (auto& component : components)
				{
					auto iterFind2 = idMap.find(component->getLinkId());
					if (iterFind2 != idMap.end())
					{
						output.push_back(RenamedGameObject());
						RenamedGameObject& renamedGO = output.back();
						renamedGO.instanceData = component->mInstanceData;
						renamedGO.originalId = component->getInstanceId();

						component->mInstanceData->mInstanceId = iterFind2->second;
					}
				}
			}

			UINT32 numChildren = current.so->getNumChildren();
			for (UINT32 i = 0; i < numChildren; i++)
			{
				HSceneObject child = current.so->getChild(i);

				if (iterFind != linkToInstanceId.end())
				{
					if (child->getLinkId() != -1)
					{
						UnorderedMap<UINT32, UINT64>& idMap = iterFind->second;

						auto iterFind2 = idMap.find(child->getLinkId());
						if (iterFind2 != idMap.end())
						{
							output.push_back(RenamedGameObject());
							RenamedGameObject& renamedGO = output.back();
							renamedGO.instanceData = child->mInstanceData;
							renamedGO.originalId = child->getInstanceId();

							child->mInstanceData->mInstanceId = iterFind2->second;
						}
					}
				}

				todo.push({ child, childParentUUID });
			}
		}
	}

	void PrefabDiff::restoreInstanceIds(const Vector<RenamedGameObject>& renamedObjects)
	{
		for (auto& renamedGO : renamedObjects)
			renamedGO.instanceData->mInstanceId = renamedGO.originalId;
	}

	RTTITypeBase* PrefabDiff::getRTTIStatic()
	{
		return PrefabDiffRTTI::instance();
	}

	RTTITypeBase* PrefabDiff::getRTTI() const
	{
		return PrefabDiff::getRTTIStatic();
	}
}