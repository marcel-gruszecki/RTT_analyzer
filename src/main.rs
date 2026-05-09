mod app;
mod communication;

fn main() {
    let native_options = eframe::NativeOptions::default();
    eframe::run_native("Multi Task Analyser", native_options, Box::new(|cc| Ok(Box::new(app::MyEguiApp::new(cc)))));

}
