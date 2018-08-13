//********************************** Banshee Engine (www.banshee3d.com) **************************************************//
//**************** Copyright (c) 2016 Marko Pintera (marko.pintera@gmail.com). All rights reserved. **********************//
#pragma once

#include "BsScriptEnginePrerequisites.h"
#include "BsScriptObject.h"
#include "Image/BsTexture.h"
#include "Wrappers/BsScriptResource.h"
#include "BsMonoClass.h"
#include "Reflection/BsRTTIType.h"

namespace bs
{
	/** @addtogroup ScriptInteropEngine
	 *  @{
	 */

	/**	Interop class between C++ & CLR for RRefBase and RRef<T>. */
	class BS_SCR_BE_EXPORT ScriptRRefBase : public ScriptObject<ScriptRRefBase>
	{
	public:
		SCRIPT_OBJ(ENGINE_ASSEMBLY, "BansheeEngine", "RRefBase")

		/**	Returns a weak handle to the resource referenced by this object. */
		ResourceHandle<Resource> getHandle() const { return mResource; }

		/** Returns the managed version of this object. */
		MonoObject* getManagedInstance() const;

		/**
		 * Creates a new managed RRefBase for the provided resource.
		 *
		 * @param[in]	handle	Handle to the resource to wrap.
		 * @param[in]	rawType	Class of the RRef type to use for wrapping the resource. If null then the resource
		 *						will be wrapped in a non-specific RRefBase object. Otherwise it will be wrapped in a
		 *						templated RRef<T> object. In the latter case caller is responsible for ensuring the
		 *						template parameter of RRef matches the actual resource type.
		 */
		template<class T>
		static ScriptRRefBase* create(const ResourceHandle<T>& handle, ::MonoClass* rawType = nullptr)
		{
			return createInternal(handle, rawType);
		}

		/** Creates a RRef type with the provided class bound as its template parameter. */
		static ::MonoClass* bindGenericParam(::MonoClass* param);

	private:
		ScriptRRefBase(MonoObject* instance, ResourceHandle<Resource> handle);
		~ScriptRRefBase();

		/** @copydoc ScriptObjectBase::_clearManagedInstance */
		void _clearManagedInstance() override;

		/** @copydoc ScriptObjectBase::_onManagedInstanceDeleted */
		void _onManagedInstanceDeleted(bool assemblyRefresh) override;

		/** @copydoc create() */
		static ScriptRRefBase* createInternal(const ResourceHandle<Resource>& handle, ::MonoClass* type = nullptr);

		ResourceHandle<Resource> mResource;
		UINT32 mGCHandle;

		/************************************************************************/
		/* 								CLR HOOKS						   		*/
		/************************************************************************/
		static bool internal_IsLoaded(ScriptRRefBase* thisPtr);
		static MonoObject* internal_GetResource(ScriptRRefBase* thisPtr);
		static void internal_GetUUID(ScriptRRefBase* thisPtr, UUID* uuid);
		static MonoObject* internal_CastAs(ScriptRRefBase* thisPtr, MonoReflectionType* type);
	};

	/** @} */
}