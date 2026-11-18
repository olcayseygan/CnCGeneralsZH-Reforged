/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "postprocess.h"

#include <string>

const char * DX11Post_Effect_Name(DX11PostEffect effect)
{
	switch (effect) {
	case DX11_POST_FXAA:
		return "fxaa";
	case DX11_POST_SHARPEN:
		return "sharpen";
	case DX11_POST_AO:
		return "ao";
	case DX11_POST_BLOOM:
		return "bloom";
	default:
		return "copy";
	}
}

bool DX11Post_Parse_Chain(const char * text, DX11PostEffect effects[DX11_POST_CHAIN_LIMIT],
	unsigned & count)
{
	count = 0;
	if (text == NULL) {
		return false;
	}

	std::string remaining(text);
	while (!remaining.empty()) {
		const std::string::size_type separator = remaining.find(',');
		std::string name = remaining.substr(0, separator);
		remaining = (separator == std::string::npos) ? std::string() : remaining.substr(separator + 1);

		if (name.empty()) {
			continue;
		}
		if (name == "off" || name == "none") {
			count = 0;
			return true;
		}
		if (count == DX11_POST_CHAIN_LIMIT) {
			count = 0;
			return false;
		}

		if (name == "copy") {
			effects[count++] = DX11_POST_COPY;
		}
		else if (name == "fxaa") {
			effects[count++] = DX11_POST_FXAA;
		}
		else if (name == "sharpen") {
			effects[count++] = DX11_POST_SHARPEN;
		}
		else if (name == "ao") {
			effects[count++] = DX11_POST_AO;
		}
		else if (name == "bloom") {
			// Bloom decides the format the scene is kept in and hands the frame back in eight bits,
			// so a chain cannot reach it after something else has already read the scene. Refusing
			// beats quietly reordering: "fxaa,bloom" is a request somebody meant, and it is not one
			// this can answer.
			if (count != 0) {
				count = 0;
				return false;
			}
			effects[count++] = DX11_POST_BLOOM;
		}
		else {
			count = 0;
			return false;
		}
	}
	return true;
}
