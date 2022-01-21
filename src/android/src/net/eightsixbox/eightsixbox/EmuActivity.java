package src.android.src.net.eightsixbox.eightsixbox;

import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.InputEvent;
import android.view.InputDevice;
import android.view.View;
import org.qtproject.qt.android.bindings.QtActivity;

public class EmuActivity extends QtActivity implements View.OnCapturedPointerListener
{
    private static final String TAG = "86BoxEmuActivity";
    private native void onKeyDownEvent(int keyCode, boolean down);
    private native void onMouseMoveEvent(float x, float y, float z);
    private native void onMouseButtonEvent(boolean pressed, int button);
    private boolean pointerCaptured = false;
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
    }
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event)
    {
    	if (event.getSource() == InputDevice.SOURCE_MOUSE && keyCode == 4) return false;
        onKeyDownEvent(keyCode, true);
        boolean res = super.onKeyDown(keyCode, event);
        return res;
    }
    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event)
    {
    	if (event.getSource() == InputDevice.SOURCE_MOUSE && keyCode == 4) return false;
        onKeyDownEvent(keyCode, false);
        return super.onKeyDown(keyCode, event);
    }
    public void uncaptureMouse()
    {
        View view = this.findViewById(android.R.id.content);
        view.releasePointerCapture();
        view.setOnCapturedPointerListener(null);
        pointerCaptured = false;
    }
    @Override
    public boolean dispatchTrackballEvent(MotionEvent motionEvent)
    {
        if (!pointerCaptured) return super.dispatchTrackballEvent(motionEvent);
        boolean pressed = false;
        if (motionEvent.getActionMasked() == MotionEvent.ACTION_BUTTON_PRESS)
        {
            pressed = true;
        }
        else
        {
            pressed = false;
        }
        if (motionEvent.getActionMasked() == MotionEvent.ACTION_BUTTON_PRESS || motionEvent.getActionMasked() == MotionEvent.ACTION_BUTTON_RELEASE)
        {
            onMouseButtonEvent(pressed, motionEvent.getButtonState());
        }
        else onMouseMoveEvent(motionEvent.getX(), motionEvent.getY(), motionEvent.getAxisValue(MotionEvent.AXIS_VSCROLL));
        return true;
    }
    @Override
    public boolean onCapturedPointer(View view, MotionEvent event)
    {
        return true;
    }
    public void captureMouse()
    {
        View view = this.findViewById(android.R.id.content);
        view.setOnCapturedPointerListener(this);
        view.requestPointerCapture();
        pointerCaptured = true;
    }
}
