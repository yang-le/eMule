#pragma once

#include "enbitmap.h"

class CSplashScreen : public CDialog
{
	DECLARE_DYNAMIC(CSplashScreen)

	enum { IDD = IDD_SPLASH };

public:
	explicit CSplashScreen(CWnd* pParent = NULL);   // standard constructor
	virtual ~CSplashScreen();

protected:
	CBitmap m_imgSplash;

	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	BOOL OnInitDialog();
	BOOL PreTranslateMessage(MSG* pMsg);

	DECLARE_MESSAGE_MAP()
	void OnPaint();
};