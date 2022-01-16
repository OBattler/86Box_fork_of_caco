package net.eightsixbox.eightsixbox;

import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import org.qtproject.qt.android.bindings.QtActivity;

public class EmuActivity extends QtActivity
{
    private static final String TAG = "86BoxEmuActivity";
    private native void onKeyDownEvent(int keyCode, boolean down);
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
    }
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event)
    {
        onKeyDownEvent(keyCode, true);
        boolean res = super.onKeyDown(keyCode, event);
        return res;
    }
    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event)
    {
        onKeyDownEvent(keyCode, false);
        return super.onKeyDown(keyCode, event);
    }
}
