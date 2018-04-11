#pragma once
#include "GDIThread.h"
#include "enbitmap.h"

/////////////////////////////////////////////////////////////////////////////
// CCreditsDlg dialog

class CCreditsDlg : public CDialog
{
	enum { IDD = IDD_ABOUTBOX };

// Construction
public:
	void KillThread();
	void StartThread();
	explicit CCreditsDlg(CWnd* pParent = NULL);   // standard constructor
	~CCreditsDlg();

	CClientDC*	m_pDC;
	CRect		m_rectScreen;

	CGDIThread* m_pThread;

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CCreditsDlg)
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CCreditsDlg)
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	virtual BOOL OnInitDialog();
	virtual void OnPaint();
	afx_msg void OnDestroy();
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	CBitmap m_imgSplash;
};