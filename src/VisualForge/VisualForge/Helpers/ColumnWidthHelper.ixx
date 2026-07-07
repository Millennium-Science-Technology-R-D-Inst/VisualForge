/*
 * FILE:      Helpers/ColumnWidthHelper.ixx
 * PURPOSE:   Save / restore DataColumn widths via AppSettingsDatabase.
 *
 * LICENSE:   Attribution-NonCommercial-ShareAlike 4.0 International
 */
module;
#include "pch.h"
#include <winrt/Microsoft.UI.Xaml.h>

export module Helpers.ColumnWidthHelper;

import Core.AppSettingsDatabase;

export namespace Helpers
{
	inline void SaveColumnWidth(std::string const& key, double width)
	{
		if (width > 0)
			Core::AppSettingsDatabase::Instance().SetDouble(
				Core::AppSettingsDatabase::CAT_COLUMN_WIDTH, key, width);
	}

	inline double GetColumnWidth(std::string const& key, double defaultWidth = 0.0)
	{
		return Core::AppSettingsDatabase::Instance().GetDouble(
			Core::AppSettingsDatabase::CAT_COLUMN_WIDTH, key).value_or(defaultWidth);
	}

	/// Restore a column's DesiredWidth from persisted pixel value.
	/// Does nothing if no saved width exists.
	template <typename TColumn>
	inline void RestoreColumn(TColumn const& col, std::string const& key)
	{
		double width = GetColumnWidth(key);
		if (width > 0)
		{
			col.Width(width);
		}
	}

	// Get the max width row of the selected column
	template <typename TColumns>
	inline winrt::hstring GetColumnMaxWidth(TColumns const& columns)
	{
		for (auto const& col : columns)
		{

		}
	}
}
