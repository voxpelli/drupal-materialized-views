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
    '#title' => t('Operations'),
  );
  
  $form['ops']['index'] = array(
    '#type' => 'submit',
    '#value' => t('Run indexing batch'),
  );

  $form['ops']['reset_indexing'] = array(
    '#type' => 'submit',
    '#value' => t('Reset indexing'),
  );

  $form['ops']['reconcile'] = array(
    '#type' => 'submit',
    '#value' => t('Reconcile the schema'),
  );
  
  return $form;
}

function _materialized_view_indexing_status() {
  $content = array();

  $cols = array(
    t('Entity type'),
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
      $max_indexed_id = db_result(db_query('SELECT max_indexed_id FROM {materialized_view_indexing} WHERE entity_type = "%s"', $entity_type_name));
      
      if ($max_indexed_id == 1) {
        $max_indexed_id = t('None (indexing complete)');
      }
      else if (!$max_indexed_id) {
        $max_indexed_id = t('None (indexing has not started)');        
      }
      
      $max_id_sql = 'SELECT MAX(' . db_escape_table($entity_type_info['id_column']) . ') FROM {' . db_escape_table($entity_type_info['id_table']) . '}';
      $max_id = db_result(db_query($max_id_sql));

      
      $rows[] = array(
        check_plain($entity_type_info['title']),
        $max_indexed_id,
        $max_id,
      );
    }
    
    $content[] = theme('table', $cols, $rows);
  }

  return implode("\n", $content);
}

function materialized_view_admin_ops_form_submit($form, &$form_state) {
  $op = $form_state['clicked_button']['#value'];
  
  if ($op == t('Reconcile the schema')) {
    materialized_view_reconcile();
    drupal_set_message('Materialized views reconciled.');
  }
  if ($op == t('Run indexing batch')) {
    materialized_view_index();
    drupal_set_message('Ran an indexing batch.');
  }
  if ($op == t('Reset indexing')) {
    db_query('TRUNCATE {materialized_view_indexing}');
    drupal_set_message('Indexing reset.');
  }
}
