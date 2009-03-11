<?php

function materialized_view_status() {
  $content = array();

  $content[] = '<br/>';
  $content[] = drupal_get_form('materialized_view_admin_ops_form');
  $content[] = _materialized_view_indexing_status();

  return implode("\n", $content);
}

function materialized_view_admin_ops_form() {
  $form = array();
  
  $form['ops'] = array(
    '#type' => 'fieldset',
    '#title' => t('Global materialized view operations'),
  );
  
  $form['ops']['index'] = array(
    '#type' => 'submit',
    '#value' => t('Run indexing batch'),
  );

  $form['ops']['reconcile'] = array(
    '#type' => 'submit',
    '#value' => t('Reconcile the schema'),
  );

  $form['ops'][] = array(
    '#value' => '&nbsp;&nbsp;&nbsp;',
  );

  $form['ops']['reset_indexing'] = array(
    '#type' => 'submit',
    '#value' => t('Reset indexing'),
  );

  $form['ops']['drop_and_reinstall'] = array(
    '#type' => 'submit',
    '#value' => t('Drop and reinstall'),
  );
  
  return $form;
}

function _materialized_view_indexing_status() {
  $content = array();

  $cols = array(
    t('Entity type'),
    t('Progress'),
    t('Max indexed ID'),
    t('Max ID'),
  );

  $materialized_views = materialized_view_get();
  foreach ($materialized_views as $materialized_view) {
    $content[] = '<h3>' . check_plain($materialized_view->getName()) . '</h3>';

    // Get a list of all entity types.
    $entity_types = materialized_view_entity_type_get();

    $rows = array();

    // Index a batch from each entity type.
    foreach ($entity_types as $entity_type_name => $entity_type_info) {
      $max_indexed_id = db_result(db_query('SELECT max_indexed_id FROM {materialized_view_indexing} WHERE mvid = "%s" AND entity_type = "%s"', $materialized_view->getName(), $entity_type_name));
      

      $max_id_sql = 'SELECT MAX(' . db_escape_table($entity_type_info['id_column']) . ') FROM {' . db_escape_table($entity_type_info['id_table']) . '}';
      $max_id = db_result(db_query($max_id_sql));
      
      if ($max_indexed_id === FALSE) {
        $max_indexed_id = t('None (indexing has not started)');
        $ratio = 0;
      }
      else if ($max_indexed_id <= 1) {
        $max_indexed_id = t('None (indexing complete)');
        $ratio = 1;
      }
      else {
        $ratio = (($max_id - $max_indexed_id) / $max_id);
      }

      $progress_width = 200;
      $green = round($ratio * $progress_width) ;
      $percentage = round($ratio * 100, 2) . '%';

      if ($ratio > .8) {
        $percentage_left = $percentage;
        $percentage_right = '';
      }
      else {
        $percentage_left = '';
        $percentage_right = $percentage;      
      }

      $progress = '';
      if ($green > 0) {
        $progress .= '<div style="float: left; padding-right: 5px; height: 20px; width: ' . $green . 'px; background-color: green; color: white; text-align: right;">' . $percentage_left . '</div>';
      }
      if ($progress_width - $green > 0) {
        $progress .= '<div style="float: left; padding-left: 5px; height: 20px; width: ' . ($progress_width - $green) . 'px; background-color: #CCCCCC;">' . $percentage_right . '</div>';
      }
      
      $rows[] = array(
        array(
          'data' => check_plain($entity_type_info['title']),
          'style' => 'width: 100px',
        ),
        array(
          'data' => $progress,
          'style' => 'width: 210px',
        ),
        array(
          'data' => $max_indexed_id,
          'style' => 'width: 250px',
        ),
        array(
          'data' => $max_id,
          'style' => 'width: 75px',
        ),
      );
    }
    
    $content[] = theme('table', $cols, $rows);
  }

  return implode("\n", $content);
}

function materialized_view_admin_ops_form_submit($form, &$form_state) {
  $op = $form_state['clicked_button']['#value'];
  
  if ($op == t('Reconcile the schema')) {
    $effect = materialized_view_reconcile();
    if ($effect) {
      drupal_set_message('Materialized views reconciled. Tables were affected.');
    }
    else {
      drupal_set_message('Materialized views reconciled. No effect.');    
    }
  }
  else if ($op == t('Run indexing batch')) {
    materialized_view_index();
    drupal_set_message('Ran an indexing batch.');
  }
  else if ($op == t('Reset indexing')) {
    db_query('TRUNCATE {materialized_view_indexing}');
    drupal_set_message('Indexing reset.');
  }
  else if ($op == t('Drop and reinstall')) {
    $materialized_views = materialized_view_get();
    foreach ($materialized_views as $materialized_view) {
      $materialized_view->installSchema();
    }
    drupal_set_message('Materialized views dropped and reinstalled.');
  }
}
