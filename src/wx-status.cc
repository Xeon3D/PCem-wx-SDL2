#include "wx-status.h"
#include "wx-common.h"
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <sstream>

extern "C" {
int get_status(char*, char*);
int fps;
int updatestatus;
drive_info_t* get_machine_info(char*, int*);
}

int show_status = 0;
int show_speed_history = 0;
int show_disc_activity = 1;
int show_machine_info = 1;
int show_mount_paths = 0;

#define ACTIVITY_TEXT wxT("Activity")

#define MAX(a, b) (a > b ? a : b)

#define render_disc_activity(x, y) do {                         \
if (show_disc_activity)                                         \
{                                                               \
        int w = 8;                                              \
        int h = 8;                                              \
        if (info.readflash) {                                   \
                dc.SetBrush(wxBrush(wxColour(0,255,0)));        \
        } else {                                                \
                dc.SetBrush(wxBrush(wxColour(0,100,0)));        \
        }                                                       \
	dc.DrawRectangle(x+bitmap.GetWidth()-w, y, w, h);       \
}} while (0)

StatusTimer::StatusTimer(StatusPane* pane)
{
        this->pane = pane;
}

void StatusTimer::Notify()
{
        pane->Refresh();
}

void StatusTimer::Start()
{
        wxTimer::Start(10);
}

BEGIN_EVENT_TABLE(StatusPane, wxPanel) EVT_PAINT(StatusPane::PaintEvent)
END_EVENT_TABLE()

StatusPane::StatusPane(wxFrame* parent) :
                wxPanel(parent)
{
#ifndef __WXOSX_MAC__
        SetDoubleBuffered(true);
#endif
        machineInfoText[0] = statusMachineText[0] = statusDeviceText[0] = 0;
        lastSpeedUpdate = 0;
        memset(speedHistory, -1, sizeof(speedHistory));

        bitmapFDD[0] = wxXmlResource::Get()->LoadBitmap("BITMAP_FLOPPY");
        bitmapFDD[1] = bitmapFDD[0].ConvertToDisabled();
        bitmapHDD[0] = wxXmlResource::Get()->LoadBitmap("BITMAP_HARDDISK");
        bitmapHDD[1] = bitmapHDD[0].ConvertToDisabled();
        bitmapCDROM[0] = wxXmlResource::Get()->LoadBitmap("BITMAP_CDROM");
        bitmapCDROM[1] = bitmapCDROM[0].ConvertToDisabled();
}

StatusPane::~StatusPane() {
}

void StatusPane::PaintEvent(wxPaintEvent& evt)
{
        wxPaintDC dc(this);
        Render(dc);
}

void StatusPane::PaintNow()
{
        wxClientDC dc(this);
        Render(dc);
}

void StatusPane::Render(wxDC& dc)
{
        wxSize cSize = GetParent()->GetClientSize();

        int width = 0;
        int height = 0;
        wxLongLong millis = wxGetUTCTimeMillis();

        dc.SetBackground(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        dc.Clear();

        // draw model/cpu etc
        if (show_machine_info) {
                int num_info;
                drive_info_t* drive_info = get_machine_info(machineInfoText, &num_info);
                wxSize size = dc.GetMultiLineTextExtent(machineInfoText);
                dc.DrawText(machineInfoText, 5, height+5);

                width = MAX(width, width+5+size.x);
                height = MAX(height, height+5+size.y);

                int x = cSize.x-5;

                for (int i = 0; i < num_info; ++i)
                {
                        drive_info_t info = show_mount_paths ? drive_info[i] : drive_info[num_info-i-1];
                        wxBitmap bitmap;
                        if (info.type == DRIVE_TYPE_FDD)
                                bitmap = info.enabled ? bitmapFDD[0] : bitmapFDD[1];
                        else if (info.type == DRIVE_TYPE_CDROM)
                                bitmap = info.enabled ? bitmapCDROM[0] : bitmapCDROM[1];
                        else
                                bitmap = info.enabled ? bitmapHDD[0] : bitmapHDD[1];
                        if (show_mount_paths)
                        {
                                x = 5;
                                dc.DrawBitmap(bitmap, x, height+5);
                                render_disc_activity(x, height+5);
                                x += bitmap.GetWidth()+5;

                                std::ostringstream ss;
                                ss << info.drive_letter << ":" << " " << info.fn;
                                std::string s = ss.str();
                                size = dc.GetTextExtent(s);
                                dc.DrawText(s, x, height+5+(bitmap.GetHeight()-size.y)/2);
                                width = MAX(width, x+size.x);
                                height = MAX(height, height+5+MAX(bitmap.GetHeight(), size.y));
                        } else {
                                x -= bitmap.GetWidth();
                                dc.DrawBitmap(bitmap, x, 5);
                                render_disc_activity(x, 5);
                                x -= 5;
                                height = MAX(height, 5+bitmap.GetHeight());
                        }
                }
        }

        if (emulation_state != EMULATION_STOPPED)
        {
                // draw status text
                if (show_status) {
                        if (updatestatus) {
                                updatestatus = 0;
                                get_status(statusMachineText, statusDeviceText);
                        }
                        if (statusMachineText) {
                                int statusX = 5;
                                int statusY = height+5;
                                wxSize size = dc.GetMultiLineTextExtent(statusMachineText);
                                dc.DrawText(statusMachineText, statusX, statusY);
                                width = MAX(width, statusX+size.GetWidth());
                                height = MAX(height, statusY+size.GetHeight());
                                if (statusDeviceText) {
                                        wxSize dSize = dc.GetMultiLineTextExtent(statusDeviceText);
                                        dc.DrawText(statusDeviceText, statusX+ceil((size.GetWidth()+50)/100.0)*100, statusY);
                                        width = MAX(width, statusX+ceil((size.GetWidth()+50)/100.0)*100+ceil((dSize.GetWidth()+50)/100.0)*100);
                                        height = MAX(height, statusY+dSize.GetHeight());
                                }
                        }
                }

                if (emulation_state == EMULATION_RUNNING && (millis-lastSpeedUpdate > 500)) {
                        lastSpeedUpdate = millis;
                        memmove(speedHistory+1, speedHistory, SPEED_HISTORY_LENGTH-2);
                        speedHistory[0] = fps;
                }
                // draw speed history
                if (show_speed_history) {
                        int speedGraphBorder = 5;
                        int speedGraphX = 5;
                        int speedGraphY = height+5;
                        int speedGraphWidth = MAX(2*SPEED_HISTORY_LENGTH, width-5);
                        double lineLength = (double)(speedGraphWidth-2)/(SPEED_HISTORY_LENGTH-1);
                        int speedGraphHeight = 125;
                        dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
                        dc.DrawRectangle(speedGraphX, speedGraphY, speedGraphWidth, speedGraphHeight+speedGraphBorder*2);
                        dc.SetPen(wxPen(wxColour(200, 200, 200)));
                        dc.DrawLine(speedGraphX, speedGraphY+speedGraphBorder+speedGraphHeight-0, speedGraphX+speedGraphWidth, speedGraphY+speedGraphHeight+speedGraphBorder-0); // 0
                        dc.DrawLine(speedGraphX, speedGraphY+speedGraphBorder+speedGraphHeight-50, speedGraphX+speedGraphWidth, speedGraphY+speedGraphHeight+speedGraphBorder-50); // 50
                        dc.DrawLine(speedGraphX, speedGraphY+speedGraphBorder+speedGraphHeight-100, speedGraphX+speedGraphWidth, speedGraphY+speedGraphHeight+speedGraphBorder-100); // 100
                        dc.DrawLine(speedGraphX, speedGraphY+speedGraphBorder+speedGraphHeight-125, speedGraphX+speedGraphWidth, speedGraphY+speedGraphHeight+speedGraphBorder-125); // 125
                        dc.SetPen(wxPen(wxColour(0, 0, 0)));
                        for (int i = 0; i < SPEED_HISTORY_LENGTH-1; ++i) {
                                int v0 = speedHistory[i];
                                int v1 = speedHistory[i+1];
                                if (v0 >= 0 && v1 >= 0) {
                                        dc.DrawLine(speedGraphX+speedGraphWidth-i*lineLength-1, speedGraphY+speedGraphHeight+speedGraphBorder-v0,
                                                        speedGraphX+speedGraphWidth-(i+1)*lineLength-1, speedGraphY+speedGraphHeight+speedGraphBorder-v1);
                                }
                        }
                        dc.SetBrush(wxBrush(wxColour(255, 255, 255), wxBrushStyle(wxBRUSHSTYLE_TRANSPARENT)));
                        dc.DrawRectangle(speedGraphX, speedGraphY, speedGraphWidth, speedGraphHeight+speedGraphBorder*2);

                        width = MAX(width, (speedGraphX+speedGraphWidth));
                        height = MAX(height, speedGraphY+speedGraphHeight+speedGraphBorder*2);
                }
        }

        width += 5;
        height += 5;
        width = width < DEFAULT_WINDOW_WIDTH ? DEFAULT_WINDOW_WIDTH : width;
        height = height < DEFAULT_WINDOW_HEIGHT ? DEFAULT_WINDOW_HEIGHT : height;
        if (cSize.GetWidth() != width || cSize.GetHeight() != height) {
                GetParent()->SetClientSize(width, height);
        }
}
