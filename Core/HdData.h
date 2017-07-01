#pragma once
#include "stdafx.h"
#include "PPU.h"
#include "../Utilities/HexUtilities.h"

struct HdTileKey
{
	static const uint32_t NoTile = -1;

	uint32_t PaletteColors;
	uint8_t TileData[16];
	uint32_t TileIndex;
	bool IsChrRamTile = false;
	bool ForDefaultKey = false;

	HdTileKey GetKey(bool defaultKey)
	{
		if(defaultKey) {
			HdTileKey copy = *this;
			copy.PaletteColors = 0xFFFFFFFF;
			return copy;
		} else {
			return *this;
		}
	}

	uint32_t GetHashCode() const
	{
		if(IsChrRamTile) {
			return CalculateHash((uint8_t*)&PaletteColors, 20);
		} else {
			uint64_t key = TileIndex | ((uint64_t)PaletteColors << 32);
			return CalculateHash((uint8_t*)&key, sizeof(key));
		}
	}

	size_t operator() (const HdTileKey &tile) const {
		return tile.GetHashCode();
	}

	bool operator==(const HdTileKey &other) const
	{
		if(IsChrRamTile) {
			return memcmp((uint8_t*)&PaletteColors, (uint8_t*)&other.PaletteColors, 20) == 0;
		} else {
			uint64_t key = TileIndex | ((uint64_t)PaletteColors << 32);
			uint64_t otherKey = other.TileIndex | ((uint64_t)other.PaletteColors << 32);
			return key == otherKey;
		}
	}

	uint32_t CalculateHash(const uint8_t* key, size_t len) const
	{
		uint32_t result = 0;
		for(size_t i = 0; i < len; i += 4) {
			result += *((uint32_t*)key);
			result = (result << 2) | (result >> 30);
			key += 4;
		}
		return result;
	}

	bool IsSpriteTile()
	{
		return (PaletteColors & 0xFF000000) == 0xFF000000;
	}
};

namespace std {
	template <> struct hash<HdTileKey>
	{
		size_t operator()(const HdTileKey& x) const
		{
			return x.GetHashCode();
		}
	};
}

struct HdPpuTileInfo : public HdTileKey
{
	uint8_t OffsetX;
	uint8_t OffsetY;
	bool HorizontalMirroring;
	bool VerticalMirroring;
	bool BackgroundPriority;
	uint8_t BgColorIndex;
	uint8_t SpriteColorIndex;
	uint8_t BgColor;
	uint8_t SpriteColor;
	uint8_t NametableValue;
};

struct HdPpuPixelInfo
{
	HdPpuTileInfo Tile;
	HdPpuTileInfo Sprite;
};

enum class HdPackConditionType
{
	TileAtPosition,
	SpriteAtPosition,
	TileNearby,
	SpriteNearby,
};

struct HdPackCondition
{
	string Name;
	HdPackConditionType Type;
	int32_t TileX;
	int32_t TileY;
	uint32_t PaletteColors;
	int32_t TileIndex;
	uint8_t TileData[16];

	bool CheckCondition(HdPpuPixelInfo *screenTiles, int x, int y)
	{
		switch(Type) {
			case HdPackConditionType::TileAtPosition:
			case HdPackConditionType::SpriteAtPosition: {
				int pixelIndex = (TileY << 8) + TileX;
				if(pixelIndex < 0 || pixelIndex > PPU::PixelCount) {
					return false;
				}

				HdPpuTileInfo &tile = Type == HdPackConditionType::TileAtPosition ? screenTiles[pixelIndex].Tile : screenTiles[pixelIndex].Sprite;
				if(TileIndex >= 0) {
					return tile.PaletteColors == PaletteColors && tile.TileIndex == TileIndex;
				} else {
					return tile.PaletteColors == PaletteColors && memcmp(tile.TileData, TileData, sizeof(TileData)) == 0;
				}
				break;
			}

			case HdPackConditionType::TileNearby: 
			case HdPackConditionType::SpriteNearby: {
				int pixelIndex = ((y + TileY) << 8) + TileX + x;
				if(pixelIndex < 0 || pixelIndex > PPU::PixelCount) {
					return false;
				}

				HdPpuTileInfo &tile = Type == HdPackConditionType::TileNearby ? screenTiles[pixelIndex].Tile : screenTiles[pixelIndex].Sprite;
				if(TileIndex >= 0) {
					return tile.TileIndex == TileIndex;
				} else {
					return memcmp(tile.TileData, TileData, sizeof(TileData)) == 0;
				}
				break;
			}
		}

		return false;
	}

	string ToString()
	{
		stringstream out;
		out << "<condition>" << Name << ",";
		switch(Type) {
			case HdPackConditionType::TileAtPosition: out << "tileAtPosition"; break;
			case HdPackConditionType::SpriteAtPosition: out << "spriteAtPosition"; break;
			case HdPackConditionType::TileNearby: out << "tileNearby"; break;
			case HdPackConditionType::SpriteNearby: out << "spriteNearby"; break;
		}
		out << ",";
		out << TileX << ",";
		out << TileY << ",";
		if(TileIndex >= 0) {
			out << TileIndex << ",";
		} else {
			for(int i = 0; i < 16; i++) {
				out << HexUtilities::ToHex(TileData[i]);
			}
		}
		out << HexUtilities::ToHex(PaletteColors, true);
		
		return out.str();
	}
};

struct HdPackTileInfo : public HdTileKey
{
	uint32_t X;
	uint32_t Y;
	uint32_t BitmapIndex;
	uint8_t Brightness;
	bool DefaultTile;
	bool Blank;
	vector<uint32_t> HdTileData;
	uint32_t ChrBankId;

	vector<HdPackCondition*> Conditions;

	bool MatchesCondition(HdPpuPixelInfo *screenTiles, int x, int y)
	{
		for(HdPackCondition* condition : Conditions) {
			if(!condition->CheckCondition(screenTiles, x, y)) {
				return false;
			}
		}
		return true;
	}

	vector<uint32_t> ToRgb()
	{
		vector<uint32_t> rgbBuffer;
		uint32_t* palette = EmulationSettings::GetRgbPalette();
		for(uint8_t i = 0; i < 8; i++) {
			uint8_t lowByte = TileData[i];
			uint8_t highByte = TileData[i + 8];
			for(uint8_t j = 0; j < 8; j++) {
				uint8_t color = ((lowByte >> (7 - j)) & 0x01) | (((highByte >> (7 - j)) & 0x01) << 1);
				uint32_t rgbColor;
				if(IsSpriteTile()) {
					rgbColor = color == 0 ? 0x00FFFFFF : palette[(PaletteColors >> ((3 - color) * 8)) & 0x3F];
				} else {
					rgbColor = palette[(PaletteColors >> ((3 - color) * 8)) & 0x3F];
				}
				rgbBuffer.push_back(rgbColor);
			}
		}

		return rgbBuffer;
	}

	void UpdateBlankTileFlag()
	{
		for(size_t i = 0; i < HdTileData.size(); i++) {
			if(HdTileData[i] != HdTileData[0]) {
				Blank = false;
				return;
			}
		}
		Blank = true;
	}

	string ToString(int pngIndex)
	{
		stringstream out;

		if(Conditions.size() > 0) {
			out << "[";
			for(int i = 0; i < Conditions.size(); i++) {
				if(i > 0) {
					out << "&";
				}
				out << Conditions[i]->Name;
			}
			out << "]";
		}

		if(IsChrRamTile) {
			out << "<tile>" << pngIndex << ",";

			for(int i = 0; i < 16; i++) {
				out << HexUtilities::ToHex(TileData[i]);
			}
			out << "," <<
				HexUtilities::ToHex(PaletteColors, true) << "," <<
				X << "," <<
				Y << "," <<
				(double)Brightness / 255 << "," <<
				(DefaultTile ? "Y" : "N") << "," <<
				ChrBankId << "," <<
				TileIndex;
		} else {
			out << "<tile>" <<
				pngIndex << "," <<
				TileIndex << "," <<
				HexUtilities::ToHex(PaletteColors, true) << "," <<
				X << "," <<
				Y << "," <<
				(double)Brightness / 255 << "," <<
				(DefaultTile ? "Y" : "N");
		}

		return out.str();
	}
};

struct HdPackBitmapInfo
{
	vector<uint8_t> PixelData;
	uint32_t Width;
	uint32_t Height;
};

struct HdBackgroundFileData
{
	string PngName;
	uint32_t Width;
	uint32_t Height;

	vector<uint8_t> PixelData;
};

struct HdBackgroundInfo
{
	HdBackgroundFileData* Data;
	uint16_t Brightness;
	vector<HdPackCondition*> Conditions;

	uint32_t* data()
	{
		return (uint32_t*)Data->PixelData.data();
	}

	string ToString()
	{
		stringstream out;

		if(Conditions.size() > 0) {
			out << "[";
			for(int i = 0; i < Conditions.size(); i++) {
				if(i > 0) {
					out << "&";
				}
				out << Conditions[i]->Name;
			}
			out << "]";
		}

		out << Data->PngName << ",";
		out << (Brightness / 255.0);

		return out.str();
	}
};

struct HdPackData
{
	vector<HdBackgroundInfo> Backgrounds;
	vector<unique_ptr<HdBackgroundFileData>> BackgroundFileData;
	vector<unique_ptr<HdPackTileInfo>> Tiles;
	vector<unique_ptr<HdPackCondition>> Conditions;
	std::unordered_map<HdTileKey, vector<HdPackTileInfo*>> TileByKey;
	std::unordered_map<string, string> PatchesByHash;
	vector<uint32_t> Palette;
	vector<uint32_t> PaletteBackup;
	uint32_t Scale = 1;
	uint32_t Version = 0;
	uint32_t OptionFlags = 0;

	HdPackData()
	{
	}

	HdPackData(const HdPackData&) = delete;
	HdPackData& operator=(const HdPackData&) = delete;

	~HdPackData()
	{
		if(PaletteBackup.size() == 0x40) {
			EmulationSettings::SetRgbPalette(PaletteBackup.data());
		}
	}
};

enum class HdPackOptions
{
	None = 0,
	NoSpriteLimit = 1,
};