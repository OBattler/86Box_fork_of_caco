package src.android.src.net.eightsixbox.eightsixbox;

import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.InputEvent;
import android.view.InputDevice;
import android.view.View;
import org.qtproject.qt.android.bindings.QtActivity;

public class EmuActivity extends QtActivity
{
    private static final String TAG = "86BoxEmuActivity";
    private native void onKeyDownEvent(int keyCode, boolean down);
    private native void onMouseMoveEvent(float x, float y);
    private boolean pointerCaptured = false;
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
    public void uncaptureMouse()
    {
    	View view = this.findViewById(android.R.id.content);
    	view.releasePointerCapture();
    	view.setOnCapturedPointerListener(null);
    }
    @Override
    public boolean onTrackballEvent(MotionEvent motionEvent)
    {
        onMouseMoveEvent(motionEvent.getX(), motionEvent.getY());
        return true;
    }
    public void captureMouse()
    {
    	View view = this.findViewById(android.R.id.content);
    	view.requestPointerCapture();
    }
}
