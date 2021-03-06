#pragma once

#include <GuiEngine/Button.hpp>
#include <GuiEngine/Frame.hpp>
#include <GuiEngine/Label.hpp>
#include <GuiEngine/LinearLayout.hpp>


namespace inl::tool {


class NodeControl : public gui::Frame {
public:
	NodeControl();

	void SetName(std::string name);
	void SetType(std::string type);
	void SetInputPorts(std::vector<std::pair<std::string, std::string>> inputPorts);
	void SetOutputPorts(std::vector<std::pair<std::string, std::string>> outputPorts);

private:
	void UpdateTitle();
	void UpdateHeight();

private:
	gui::Label m_title;

	gui::LinearLayout m_titleLayout;
	gui::LinearLayout m_ioSplitLayout;
	gui::LinearLayout m_inputPortsLayout;
	gui::LinearLayout m_outputPortsLayout;

	std::vector<gui::Button> m_inputPorts;
	std::vector<gui::Button> m_outputPorts;

	std::string m_name;
	std::string m_type;
};


} // namespace inl::tool
