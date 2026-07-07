/*
 * FILE:      Helpers/ControlLengthHelper.ixx
 * PURPOSE:   Save / restore control widths via AppSettingsDatabase.
 *
 * LICENSE:   Attribution-NonCommercial-ShareAlike 4.0 International
 */
module;

#include "pch.h"
#include <winrt/Microsoft.UI.Xaml.h>

export module Helpers.ControlLengthHelper;

import Core.AppSettingsDatabase;

export namespace Helpers
{
	inline void SaveControlHeight(std::string const& key, double height)
	{
		if (height > 0)
			Core::AppSettingsDatabase::Instance().SetDouble(
				Core::AppSettingsDatabase::CAT_CONTROL_HEIGHT, key, height);
	}

	inline double GetControlHeight(std::string const& key, double defaultHeight = 0.0)
	{
		return Core::AppSettingsDatabase::Instance().GetDouble(
			Core::AppSettingsDatabase::CAT_CONTROL_HEIGHT, key).value_or(defaultHeight);
	}

	/// Restore a control's Height from persisted pixel value.
	/// Does nothing if no saved height exists.
	template <typename TControl>
	inline void RestoreControlHeight(TControl const& control, std::string const& key)
	{
		double h = GetControlHeight(key);
		if (h > 0)
		{
			control.Height(h);
		}
	}
}