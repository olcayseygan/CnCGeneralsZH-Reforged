/*
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

/*
 * A fullscreen device at a size the POSIX adapter's mode list does not hold.  The list stops at
 * 2560x1440 and a fullscreen game takes the desktop's size, so on a 4K screen DX8Wrapper's mode search
 * found nothing and left the display and back buffer formats UNKNOWN.  The device was made anyway, but
 * its caps then supported no texture format, and every video buffer failed: the EA logo, the sizzle and
 * the Generals Challenge load screen with its portraits.  The device is made the way
 * test_install_textures makes it, with no window and no game data.
 */
#include "test_harness.h"

#include "PreRTS.h"
#include "Common/GameMemory.h"

#include "ffactory.h"
#include "wwmath.h"
#include "ww3d.h"
#include "dx8wrapper.h"
#include "dx8caps.h"
#include "dx11runtime.h"

// The names the game's executable supplies, as test_install_textures supplies them.
static char s_noAppPrefix[] = "";
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
char *gAppPrefix = s_noAppPrefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

static SimpleFileFactoryClass s_factory;

TEST(fullscreen_at_a_size_the_mode_list_lacks_takes_the_desktop_format)
{
	// One device a process: a second Set_Render_Device starts the texture loader's thread again.
	initMemoryManager();
	_TheFileFactory = &s_factory;
	WWMath::Init();
	CHECK_EQ(WW3D::Init(ApplicationHWnd), (int)WW3D_ERROR_OK);
	WW3D::Set_Texture_Bitdepth(32);
	Direct3D11_Enable(false);
	Direct3D11_Present_Enable(false);

	CHECK_EQ(WW3D::Set_Render_Device(0, 3840, 2160, 32, 0, true), (int)WW3D_ERROR_OK);
	CHECK(DX8Wrapper::_Get_D3D_Device() != NULL);
	if (DX8Wrapper::_Get_D3D_Device() == NULL) return;

	// What W3DDisplay::createVideoBuffer asks before it makes a video buffer.
	CHECK(DX8Wrapper::getBackBufferFormat() != WW3D_FORMAT_UNKNOWN);
	CHECK(DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(WW3D_FORMAT_X8R8G8B8));
	CHECK(DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(WW3D_FORMAT_A8R8G8B8));
	CHECK(DX8Wrapper::Get_Current_Caps()->Support_DXTC());
}
