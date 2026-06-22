using System;
using System.Windows.Forms;

namespace Jdy31OledMonitor
{
    // Standard WinForms startup entry point.
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
