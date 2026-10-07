import React from 'react';
import { connect } from 'react-redux';
import { bindActionCreators } from 'redux';
import shortid from 'shortid';
import * as actionCreators from '../store/actions/actionCreators';
import { saveProjectToStorage } from '../utils/storage';

const SaveDrawing = props => {
  const save = async () => {
    const { frames, paletteGridData, cellSize, columns, rows } = props;
    const drawingToSave = {
      frames,
      paletteGridData,
      cellSize,
      columns,
      rows,
      animate: frames.size > 1,
      id: shortid.generate()
    };

    if (await saveProjectToStorage(drawingToSave)) {
      props.actions.sendNotification('Картинка сохранена');
    }
  };

  return (
    <div className="save-drawing">
      <button
        type="button"
        onClick={() => {
          save();
        }}
      >
        Сохранить
      </button>
    </div>
  );
};

const mapStateToProps = state => {
  const frames = state.present.get('frames');
  return {
    frames: frames.get('list'),
    columns: frames.get('columns'),
    rows: frames.get('rows'),
    cellSize: state.present.get('cellSize'),
    paletteGridData: state.present.getIn(['palette', 'grid'])
  };
};

const mapDispatchToProps = dispatch => ({
  actions: bindActionCreators(actionCreators, dispatch)
});

const SaveDrawingContainer = connect(mapStateToProps, mapDispatchToProps)(SaveDrawing);
export default SaveDrawingContainer;
