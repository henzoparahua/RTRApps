#include <stdafx.h>
#include "D3DApp.h"

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int CmdShow)
{
	D3DApp sample(1280, 720, L"nkrhuap");
	return Win32App::Run(&sample, hInstance, CmdShow);
}