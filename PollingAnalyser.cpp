// Some preprocessor things to handle wxWidgets needs. Look at wxWidgets's "minimal"
// sample for details on why these are needed

#include "wx/wxprec.h"

#ifdef __BORLANDC__
  #pragma hdrstop
#endif

#ifndef WX_PRECOMP
  #include "wx/wx.h"
#endif

#ifndef wxHAS_IMAGES_IN_RESOURCES
  #include "../sample.xpm"
#endif

#include "ParentFrame.h"
#include "QuickTest.h"

// Switch true/false depending on whether we want to run the actual program or just a quick test of the mechanics
constexpr bool DoQuickTest = false;

// This is wxWidgets's way of creating a new app: this class MyApp derives from
// the wxWidgets class wxApp; we then run the macro IMPLEMENT_APP below that
class MyApp : public wxApp
{
public:
	virtual bool OnInit();

  // Override OnRun to bypass the built-in try/catch
  virtual int OnRun() override
  {
    // Default wxApp::OnRun calls MainLoop() inside a try/catch.
    // We call MainLoop() directly to let exceptions bubble up to the OS/Debugger.
    return MainLoop();
  }
};

IMPLEMENT_APP(MyApp)

// 'Main program' equivalent: the program execution "starts" here (through IMPLEMENT_APP above)
bool MyApp::OnInit()
{
  // call the base class initialization method, currently it only parses a
  // few common command-line options but it could do more in the future
  if ( !wxApp::OnInit() )
     return false;

	if (DoQuickTest) {
		QuickTest test;
		return false;
	}

  wxImage::AddHandler(new wxPNGHandler);

    // create the main application window
  // this stays around until the application is closed and is then deleted
  // (it's registered in the constructor of ParentFrame so wxWidgets knows about it)
  // we don't need to (and shouldn't) delete it ourselves
  ParentFrame *frame = new ParentFrame("Polling Analyser");

  // now show it (the frames, unlike simple controls, are not shown when
  // created initially)
  frame->Show(true);

  // true means success: wxApp::OnRun() will be called which will enter the main message
  // loop and the application will run. If false was returned here, the
  // application would exit immediately.
  return true;
}