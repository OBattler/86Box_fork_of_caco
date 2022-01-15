package net.eightsixbox.eightsixbox;

import android.os.Bundle;
import org.qtproject.qt.android.bindings.QtActivity;

public class EmuActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.ENVIRONMENT_VARIABLES = "QT_USE_ANDROID_NATIVE_DIALOGS=0";
        super.onCreate(savedInstanceState);
    }
}
